#include "Client.h"

#include "FileWriter.h"

#include <grpcpp/grpcpp.h>
#include <grpc/support/log.h>
#include <signal.h>

#include <cstdlib>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <string>


using namespace FileExchange;

#if 0
enum class RequestType
{
    Download,
    Upload
};


struct RequestData
{
    RequestType type;
    std::string filename;
};


const std::string DownloadRequestArg = "download";
const std::string UploadRequestArg   = "upload";




bool parseArgs(int argc, char** argv, RequestData& requestData)
{
    const unsigned int RequestTypeArgIndex = 1;
    const unsigned int FileNameArgIndex    = 2;
    const int RequiredArgNumber            = FileNameArgIndex + 1;

    if (argc == RequiredArgNumber) {
        std::string requestTypeArg(argv[RequestTypeArgIndex]);
        if (requestTypeArg == DownloadRequestArg) {
            requestData.type = RequestType::Download;
        }
        else if (requestTypeArg == UploadRequestArg) {
            requestData.type = RequestType::Upload;
        }
        else {
            return false;
        }

        requestData.filename = argv[FileNameArgIndex];

        return true;
    }

    return false;
}


void printUsage(const std::string& appName)
{
    std::cout << "Usage: " << appName << " " << DownloadRequestArg
              << "|" << UploadRequestArg << " <file name>" << std::endl;
}
#endif

std::unique_ptr<FileExchangeClient> client;

void signalHandler(int signo)
{
    if (client) {
        client->cancel();
    }
}


void setSignalHandler()
{
#ifdef _WIN32
    signal(SIGINT, signalHandler);
#else
        struct sigaction sa;
        sa.sa_handler = signalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, NULL);
#endif
}



void runRequest(FileExchangeClient* client, const char* filename)//, const RequestData& requestData)
{
    try {
        //if (requestData.type == RequestType::Download) {
        client->download(/*requestData.*/filename, 
            [](const std::string& filename, unsigned long long) { return std::unique_ptr<IFileWriter>( new FileWriter(filename) ); });
        //}
        //else {
        //    client->upload(requestData.filename);
        //}
    }
    catch (std::exception& e) {
        gpr_log(GPR_ERROR, "Caught exception: %s", e.what());
    }
    catch (...) {
        gpr_log(GPR_ERROR, "Caught unknown exception");
    }

    gpr_log(GPR_DEBUG, "Worker thread is shutting down");
}


int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Please provide file name.\n";
        return EXIT_FAILURE;
    }

    try {
        gpr_log_verbosity_init();
        gpr_set_log_verbosity(GPR_LOG_SEVERITY_ERROR);

        //RequestData requestData;

        //if (!parseArgs(argc, argv, requestData)) {
        //    printUsage(argv[0]);
        //    return EXIT_FAILURE;
        //}
        //else {
            gpr_log(GPR_DEBUG, "Setting up signal handlers");
            setSignalHandler();

            gpr_log(GPR_DEBUG, "Starting client");
            auto channel = grpc::CreateChannel("localhost:55555", grpc::InsecureChannelCredentials());
            client = std::make_unique<FileExchangeClient>(channel);

            //gpr_log(GPR_DEBUG, "Starting worker threads");

            std::cout << "Press Ctrl-C to terminate..." << std::endl;
            std::async(std::launch::async, runRequest, client.get(), argv[1]);//std::cref(requestData));
        //}

        return EXIT_SUCCESS;
    }
    catch (std::exception& e) {
        gpr_log(GPR_ERROR, "Caught exception: %s", e.what());
    }
    catch (...) {
        gpr_log(GPR_ERROR, "Caught unknown exception");
    }

    gpr_log(GPR_DEBUG, "Main thread is shutting down abnormally");
    return EXIT_FAILURE;
}

