#include "core/Result.h"

namespace af::core
{

std::string_view toString(ErrorCode code) noexcept
{
    switch (code)
    {
    case ErrorCode::invalidArgument:
        return "invalidArgument";
    case ErrorCode::invalidData:
        return "invalidData";
    case ErrorCode::unsupported:
        return "unsupported";
    case ErrorCode::notFound:
        return "notFound";
    case ErrorCode::ioError:
        return "ioError";
    case ErrorCode::outOfMemory:
        return "outOfMemory";
    case ErrorCode::cancelled:
        return "cancelled";
    case ErrorCode::internal:
        return "internal";
    }
    return "unknown";
}

std::string Error::describe() const
{
    std::string text{toString(code)};
    text += ": ";
    text += message;
    if (!subject.empty())
    {
        text += " [";
        text += subject;
        text += ']';
    }
    return text;
}

} // namespace af::core
