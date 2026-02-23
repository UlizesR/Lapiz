#include "Lapiz/core/Lerror.h"

void LapizSetError(LapizError* error, LapizResult result, const char* message)
{
    error->result = result;
    error->message = message;
}