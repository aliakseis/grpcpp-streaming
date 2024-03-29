#include "DownloadRequestHandler.h"

#include <grpc/support/log.h>

#include <iostream>


using namespace FileExchange;


DownloadRequestHandler::DownloadRequestHandler(HandlerTag tag,
                                               SimpleFileServer::Stub* stub,
                                               grpc::CompletionQueue* cq,
                                               const std::string& filename,
                                               FileWriterFactory fileWriterFactory)
    : tag_(tag)
    , stub_(stub)
    , cq_(cq)
    , state_(CallState::NewCall)
    , filename_(filename)
    , bytesReceived_(0)
    , fileWriterFactory_(fileWriterFactory)
{
    this->onNext(true);
}


bool DownloadRequestHandler::onNext(bool ok)
{
    try {
        if (ok) {
            switch (state_) {
            case CallState::NewCall: handleNewCallState(); break;
            case CallState::SendingRequest: handleSendingRequestState(); break;
            case CallState::ExpectingHeader: handleExpectingHeaderState(); break;
            case CallState::ReceivingFile: handleReceivingFileState(); break;
            case CallState::CallComplete: handleCallCompleteState(); return false; //TODO comment
            }
        }
        else {
            state_ = CallState::CallComplete;
            rpc_->Finish(&status_, tag_);
        }

        return true;
    }
    catch (std::exception& e) {
        gpr_log(GPR_ERROR, "Download processing error: %s", e.what());
    }
    catch (...) {
        gpr_log(GPR_ERROR, "Download processing error: unknown exception caught");
    }

    if (state_ == CallState::NewCall) {
        //TODO comment
        return false;
    }

    ctx_.TryCancel();

    return true;
}


void DownloadRequestHandler::cancel()
{
    ctx_.TryCancel();
}


void DownloadRequestHandler::handleNewCallState()
{
    const std::size_t ServerDefaultChunkSize = 0;

    request_.set_name(filename_);
    request_.set_chunksize(ServerDefaultChunkSize);

    rpc_ = stub_->PrepareAsyncDownload(&ctx_, request_, cq_);

    state_ = CallState::SendingRequest;
    rpc_->StartCall(tag_);
}


void DownloadRequestHandler::handleSendingRequestState()
{
    state_ = CallState::ExpectingHeader;
    rpc_->Read(&response_, tag_);
}


void DownloadRequestHandler::handleExpectingHeaderState()
{
    if (response_.has_header()) {
        //TODO check filename?
        const auto filename = response_.header().name();
        const auto size = response_.header().size();
        fileWriter_ = fileWriterFactory_(filename, size);//std::make_unique<FileWriter>(filename);

        if (size > 0) {
            state_ = CallState::ReceivingFile;
            response_.Clear();
            rpc_->Read(&response_, tag_);
        }
        else {
            state_ = CallState::CallComplete;
            rpc_->Finish(&status_, tag_);
        }
    }
    else {
        throw std::runtime_error("File header expected");
    }
}

void DownloadRequestHandler::handleReceivingFileState()
{
    if (response_.has_chunk()) {
        auto& chunkData = response_.chunk().data();
        if (chunkData.empty()) {
            gpr_log(GPR_INFO, "Received an empty FileChunk, ignoring");
        }
        else {
            fileWriter_->write(chunkData);
            bytesReceived_ += chunkData.size();
        }

        state_ = CallState::ReceivingFile;
        response_.Clear();
        rpc_->Read(&response_, tag_);
    }
    else {
        //throw std::runtime_error("File chunk expected");
        handleExpectingHeaderState();
    }
}


void DownloadRequestHandler::handleCallCompleteState()
{
    switch (status_.error_code()) {
    case grpc::OK:
        std::cout << "[" << filename_ << "]: download complete: " << bytesReceived_ << " bytes received" << std::endl;
        break;

    case grpc::CANCELLED:
        std::cout << "[" << filename_ << "]: download cancelled" << std::endl;
        break;

    default:
        std::cout << "[" << filename_ << "]: download failed: " << status_.error_message() << std::endl;
        break;
    }
}

