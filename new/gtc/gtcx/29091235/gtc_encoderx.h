#ifndef GTC_ENCODERX_H
#define GTC_ENCODERX_H

#include "gtc_encoder.h"

#include <string>


class GtcEncoderX {
public:

    bool encodeFile(
        const std::string& inputName,
        const std::string& outputName,
        GtcEncodeStats& stats);
};


#endif
