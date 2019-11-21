#pragma once

#include <string>
#include <functional>
#include <memory>

namespace FileExchange {

struct IFileWriter
{
    ~IFileWriter() {}
    virtual void write(const std::string& buffer) = 0;
};

typedef std::function<std::unique_ptr<IFileWriter>(const std::string&, unsigned long long)> FileWriterFactory;

}
