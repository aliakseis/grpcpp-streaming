#include "DownloadRequestHandler.h"
#include "FileError.h"
#include "HandlerManager.hpp"
#include "RequestStatus.h"

#include <grpc/support/log.h>

#include <filesystem>

using namespace FileExchange;


DownloadRequestHandler::DownloadRequestHandler(HandlerTag tag,
                                               HandlerManager* handlerManager,
                                               FileManager* fileManager,
                                               SimpleFileServer::AsyncService* service,
                                               grpc::ServerCompletionQueue* cq)
    : tag_(tag)
    , handlerManager_(handlerManager)
    , fileManager_(fileManager)
    , state_(CallState::NewCall)
    , errorStatus_(nullptr)
    , service_(service)
    , cq_(cq)
    , responder_(&ctx_)
    , bytesToSend_(0)
{
    this->onNext(true);
}


DownloadRequestHandler::~DownloadRequestHandler()
{
    gpr_log(GPR_DEBUG, "[%p] The handler is destroyed", tag_);
}


bool DownloadRequestHandler::onNext(bool ok)
{
    try {
        if (state_ == CallState::CallComplete) {
            gpr_log(GPR_DEBUG, "[%p] The call has been completed", tag_);
            return false;
        }
        else if (ok) {
            switch (state_) {
            case CallState::NewCall: handleNewCallState(); break;
            case CallState::ExpectingRequest: handleExpectingRequestState(); break;
            case CallState::SendingFile: handleSendingFileState(); break;
            case CallState::SendingNextFile: startFileDownloading(); break;
            }
        }
        else {
            if (state_ == CallState::ExpectingRequest) {
                // server has been shut down before receiving a matching DownloadRequest
                gpr_log(GPR_DEBUG, "[%p] Server has been shut down before receiving a matching DownloadRequest", tag_);
                return false;
            }
            else {
                gpr_log(GPR_INFO, "[%p] DownloadRequest processing aborted: call is cancelled or connection is dropped", tag_);
                return false;
            }
        }
    }
    catch (FileNotManagedError& e) {
        errorStatus_ = &RequestStatus::FileNotFound;
        gpr_log(GPR_ERROR, "[%p] Download processing error: %s", tag_, e.what());
    }
    catch (FileLockError& e) {
        errorStatus_ = &RequestStatus::FileLocked;
        gpr_log(GPR_ERROR, "[%p] Download processing error: %s", tag_, e.what());
    }
    catch (FileIOError& e) {
        errorStatus_ = &RequestStatus::FileIOError;
        gpr_log(GPR_ERROR, "[%p] Download processing error: %s", tag_, e.what());
    }
    catch (std::exception& e) {
        errorStatus_ = &RequestStatus::UnknownError;
        gpr_log(GPR_ERROR, "[%p] Download processing error: %s", tag_, e.what());
    }
    catch (...) {
        errorStatus_ = &RequestStatus::UnknownError;
        gpr_log(GPR_ERROR, "[%p] Download processing error: unknown exception caught", tag_);
    }

    if (errorStatus_ != nullptr) {
        state_ = CallState::CallComplete;
        responder_.Finish(*errorStatus_, tag_);
    }

    return true;
}


void DownloadRequestHandler::cancel()
{
    //TODO
}


void DownloadRequestHandler::handleNewCallState()
{
    state_ = CallState::ExpectingRequest;
    service_->RequestDownload(&ctx_, &request_, &responder_, cq_, cq_, tag_);
}


void DownloadRequestHandler::handleExpectingRequestState()
{
    try {
        handlerManager_->addHandler<DownloadRequestHandler>(handlerManager_, fileManager_, service_, cq_);
    }
    catch (...) {
        gpr_log(GPR_ERROR, "[%p] Failed to create DownloadRequest handler, no new DownloadRequest's can be processed", tag_);
    }

    gpr_log(GPR_INFO, "[%p] Received DownloadRequest: file name [%s]", tag_, request_.name().c_str());

    listFiles(request_.name());

    startFileDownloading();
}


void DownloadRequestHandler::handleSendingFileState()
{
    bool finish = false;

    auto chunkSize = request_.chunksize();
    if (chunkSize >= bytesToSend_) {
        chunkSize = bytesToSend_;
        finish = true;
    }

    response_.Clear();
    auto buffer = response_.mutable_chunk()->mutable_data();
    buffer->resize(chunkSize);

    auto bytesRead = fileReader_->read(*buffer);
    GPR_DEBUG_ASSERT(bytesRead == chunkSize);

    bytesToSend_ -= chunkSize;

    if (finish) {
        if (fileNames_.empty())
        {
            state_ = CallState::CallComplete;
            responder_.WriteAndFinish(response_, grpc::WriteOptions(), grpc::Status::OK, tag_);
        }
        else
        {
            state_ = CallState::SendingNextFile;
            responder_.Write(response_, tag_);
        }
    }
    else {
        responder_.Write(response_, tag_);
    }
}

void DownloadRequestHandler::listFiles(const std::string& folder)
{
    fileNames_.clear();
    for (auto& p : std::filesystem::directory_iterator(folder))
    {
        if (p.is_regular_file())
            fileNames_.push_back(p.path().string());
    }

    std::sort(fileNames_.begin(), fileNames_.end());
}

void DownloadRequestHandler::startFileDownloading()
{
    const unsigned long DefaultChunkSize = 4 * 1024; // 4K

    if (!fileNames_.empty()) {
        const auto filename = fileNames_.front();
        fileNames_.pop_front();

        fileReader_ = fileManager_->readFile(filename);

        response_.mutable_header()->set_name(std::filesystem::path(filename).filename().string());

        bytesToSend_ = fileReader_->fileSize();
        response_.mutable_header()->set_size(bytesToSend_);

        if (bytesToSend_ > 0) {
            if (request_.chunksize() == 0) {
                request_.set_chunksize(DefaultChunkSize);
            }

            state_ = CallState::SendingFile;
            responder_.Write(response_, tag_);
        }
        else {
            state_ = CallState::CallComplete;
            responder_.WriteAndFinish(response_, grpc::WriteOptions(), grpc::Status::OK, tag_);
        }
    }
    else {
        errorStatus_ = &RequestStatus::FileNameEmpty;
    }
}
