#pragma once

#include <unistd.h>
#include <utility>

namespace gui::desktop
{

class FileDescriptorRAII
{
public:
    FileDescriptorRAII() : fileDescriptor(-1) {}

    explicit FileDescriptorRAII(int descriptorToOwn) : fileDescriptor(descriptorToOwn) {}

    FileDescriptorRAII(const FileDescriptorRAII &) = delete;
    FileDescriptorRAII &operator=(const FileDescriptorRAII &) = delete;

    FileDescriptorRAII(FileDescriptorRAII &&other) noexcept : fileDescriptor(other.fileDescriptor)
    {
        other.fileDescriptor = -1;
    }

    FileDescriptorRAII &operator=(FileDescriptorRAII &&other) noexcept
    {
        if (this != &other) {
            closeDescriptorIfValid();
            fileDescriptor = other.fileDescriptor;
            other.fileDescriptor = -1;
        }
        return *this;
    }

    ~FileDescriptorRAII()
    {
        closeDescriptorIfValid();
    }

    [[nodiscard]] int get() const
    {
        return fileDescriptor;
    }

    [[nodiscard]] bool isValid() const
    {
        return fileDescriptor >= 0;
    }

    int release()
    {
        int releasedDescriptor = fileDescriptor;
        fileDescriptor = -1;
        return releasedDescriptor;
    }

    void reset(int newDescriptor = -1)
    {
        closeDescriptorIfValid();
        fileDescriptor = newDescriptor;
    }

private:
    void closeDescriptorIfValid()
    {
        bool descriptorIsValid = (fileDescriptor >= 0);
        if (descriptorIsValid) {
            ::close(fileDescriptor);
            fileDescriptor = -1;
        }
    }

    int fileDescriptor;
};

} // namespace gui::desktop
