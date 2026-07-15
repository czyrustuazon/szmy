#include "jptext_errors.h"

const char *jptext_error_str(int code)
{
    switch (code) {
    case 1:  return "cfg:u";
    case 2:  return "C3D";
    case 3:  return "C2D";
    case 4:  return "screen target";
    case 5:  return "system font";
    case 6:  return "text buffer";
    case 7:  return "background";
    default: return "?";
    }
}
