#ifndef JWT_COMPACT_H
#define JWT_COMPACT_H

// Include standard headers.
#include <string>
#include <vector>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <algorithm>
// Optional logging (if you have logging.h in your project).
#ifdef USE_LOGGING
#include "logging.h"
#else
// If logging.h is not available, define a dummy Warning macro.
#ifndef Warning
#define Warning(...) /* no logging */
#endif
#endif

namespace jwt_compact {

    // ---------------------------------------------------------------------
    // Base64url Encode / Decode (unchanged)
    // ---------------------------------------------------------------------
    //
    // The base64url alphabet is:
    //   "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
    // Padding ('=') is omitted on output.
    // Errors (bad length or character) cause the decode function to return an empty vector.

    inline std::string base64url_encode(const std::vector<unsigned char>& data)
    {
        static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        size_t full_chunks = data.size() / 3;
        size_t rem = data.size() % 3;
        std::string encoded;
        encoded.reserve(full_chunks * 4 + (rem ? (rem + 1) : 0));
        size_t idx = 0;
        for (size_t i = 0; i < full_chunks; i++)
        {
            uint32_t triple = (static_cast<uint32_t>(data[idx]) << 16) |
                (static_cast<uint32_t>(data[idx + 1]) << 8) |
                (static_cast<uint32_t>(data[idx + 2]));
            encoded.push_back(base64_chars[(triple >> 18) & 0x3F]);
            encoded.push_back(base64_chars[(triple >> 12) & 0x3F]);
            encoded.push_back(base64_chars[(triple >> 6) & 0x3F]);
            encoded.push_back(base64_chars[triple & 0x3F]);
            idx += 3;
        }
        if (rem == 1)
        {
            uint32_t triple = (static_cast<uint32_t>(data[idx]) << 16);
            encoded.push_back(base64_chars[(triple >> 18) & 0x3F]);
            encoded.push_back(base64_chars[(triple >> 12) & 0x3F]);
            // No output for missing bytes.
        }
        else if (rem == 2)
        {
            uint32_t triple = (static_cast<uint32_t>(data[idx]) << 16) |
                (static_cast<uint32_t>(data[idx + 1]) << 8);
            encoded.push_back(base64_chars[(triple >> 18) & 0x3F]);
            encoded.push_back(base64_chars[(triple >> 12) & 0x3F]);
            encoded.push_back(base64_chars[(triple >> 6) & 0x3F]);
        }
        return encoded;
    }

    inline int decode_base64url_char(char c)
    {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '-')
            return 62;
        if (c == '_')
            return 63;
        return -1;
    }

    inline std::vector<unsigned char> base64url_decode(const std::string& input)
    {
        std::vector<unsigned char> decoded;
        // Calculate needed padding.
        size_t mod = input.size() % 4;
        if (mod == 1)
        {
            Warning("base64url_decode: invalid input length");
            return std::vector<unsigned char>(); // error
        }
        std::string padded = input;
        if (mod == 2)
            padded.append("==");
        else if (mod == 3)
            padded.append("=");

        size_t len = padded.size();
        decoded.reserve((len / 4) * 3);
        for (size_t i = 0; i < len; i += 4)
        {
            int vals[4];
            for (int j = 0; j < 4; j++)
            {
                char c = padded[i + j];
                if (c == '=')
                    vals[j] = 0;
                else
                {
                    int v = decode_base64url_char(c);
                    if (v < 0)
                    {
                        Warning("base64url_decode: invalid character encountered");
                        return std::vector<unsigned char>(); // error
                    }
                    vals[j] = v;
                }
            }
            uint32_t triple = (vals[0] << 18) | (vals[1] << 12) | (vals[2] << 6) | vals[3];
            decoded.push_back((triple >> 16) & 0xFF);
            if (padded[i + 2] != '=')
                decoded.push_back((triple >> 8) & 0xFF);
            if (padded[i + 3] != '=')
                decoded.push_back(triple & 0xFF);
        }
        return decoded;
    }

    // ---------------------------------------------------------------------
    // (The Z85 functions remain in the file but are no longer used for JWT parts)
    // ---------------------------------------------------------------------

    inline constexpr const char z85_alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#";

    inline int decode_z85_char(char c)
    {
        // Linear search the alphabet (only 85 characters).
        for (int i = 0; i < 85; i++)
        {
            if (z85_alphabet[i] == c)
                return i;
        }
        return -1;
    }

    inline std::string z85_encode(const std::vector<unsigned char>& data)
    {
        if (data.size() % 4 != 0)
        {
            Warning("z85_encode: Data length must be a multiple of 4");
            return "";
        }
        size_t blocks = data.size() / 4;
        std::string result;
        result.reserve(blocks * 5);
        for (size_t i = 0; i < blocks; i++)
        {
            uint32_t value = (static_cast<uint32_t>(data[i * 4 + 0]) << 24) |
                (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
                (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
                static_cast<uint32_t>(data[i * 4 + 3]);
            char block[5];
            for (int j = 4; j >= 0; j--)
            {
                block[j] = z85_alphabet[value % 85];
                value /= 85;
            }
            result.append(block, 5);
        }
        return result;
    }

    inline std::vector<unsigned char> z85_decode(const std::string& str)
    {
        std::vector<unsigned char> data;
        if (str.size() % 5 != 0)
        {
            Warning("z85_decode: String length must be a multiple of 5");
            return data;
        }
        size_t blocks = str.size() / 5;
        data.reserve(blocks * 4);
        for (size_t i = 0; i < blocks; i++)
        {
            uint32_t value = 0;
            for (int j = 0; j < 5; j++)
            {
                int digit = decode_z85_char(str[i * 5 + j]);
                if (digit < 0)
                {
                    Warning("z85_decode: Invalid character encountered");
                    return std::vector<unsigned char>(); // error
                }
                value = value * 85 + static_cast<uint32_t>(digit);
            }
            data.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
            data.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
            data.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
            data.push_back(static_cast<unsigned char>(value & 0xFF));
        }
        return data;
    }

    // ---------------------------------------------------------------------
    // Helper Functions: Safe Big-Endian Read / Write (unchanged)
    // ---------------------------------------------------------------------

    inline void write_uint32_be(uint32_t value, std::vector<unsigned char>& out)
    {
        out.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
        out.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
        out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
        out.push_back(static_cast<unsigned char>(value & 0xFF));
    }

    inline void write_uint16_be(uint16_t value, std::vector<unsigned char>& out)
    {
        out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
        out.push_back(static_cast<unsigned char>(value & 0xFF));
    }

    inline bool safe_read_uint32_be(const std::vector<unsigned char>& data, size_t offset, uint32_t& value)
    {
        if (offset + 4 > data.size())
        {
            Warning("safe_read_uint32_be: out of range");
            return false;
        }
        value = (static_cast<uint32_t>(data[offset]) << 24) |
            (static_cast<uint32_t>(data[offset + 1]) << 16) |
            (static_cast<uint32_t>(data[offset + 2]) << 8) |
            static_cast<uint32_t>(data[offset + 3]);
        return true;
    }

    inline bool safe_read_uint16_be(const std::vector<unsigned char>& data, size_t offset, uint16_t& value)
    {
        if (offset + 2 > data.size())
        {
            Warning("safe_read_uint16_be: out of range");
            return false;
        }
        value = (static_cast<uint16_t>(data[offset]) << 8) |
            static_cast<uint16_t>(data[offset + 1]);
        return true;
    }

    // ---------------------------------------------------------------------
    // New Base94 Encode / Decode for the signature
    // ---------------------------------------------------------------------
    //
    // This pair of functions implements a simple big–integer conversion using
    // all printable ASCII characters from '!' (33) to '~' (126) (a total of 94 characters).
    // (Using a larger alphabet than Z85 gives a slightly better efficiency.)

    inline std::string base94_encode(const std::vector<unsigned char>& data)
    {
        if (data.empty())
            return "";
        // We treat 'data' as a big–endian integer.
        std::vector<unsigned char> num = data;
        // Count any leading zero bytes (which will be represented as leading '!' characters).
        size_t leadingZeros = 0;
        while (leadingZeros < num.size() && num[leadingZeros] == 0)
            ++leadingZeros;

        std::string result;
        // Do repeated division by 94.
        while (!(num.size() == 1 && num[0] == 0))
        {
            int remainder = 0;
            std::vector<unsigned char> quotient;
            for (size_t i = 0; i < num.size(); i++)
            {
                int accumulator = remainder * 256 + num[i];
                int q = accumulator / 94;
                remainder = accumulator % 94;
                if (!(quotient.empty() && q == 0))
                    quotient.push_back(static_cast<unsigned char>(q));
            }
            result.push_back(static_cast<char>(remainder + 33)); // Map 0 -> '!'
            num = quotient;
            if (num.empty())
                break;
        }
        // Add a '!' for every leading zero byte.
        for (size_t i = 0; i < leadingZeros; i++)
            result.push_back('!');

        std::reverse(result.begin(), result.end());
        return result;
    }

    inline std::vector<unsigned char> base94_decode(const std::string& input)
    {
        // We will simulate accumulating a big–integer in base 256.
        std::vector<unsigned char> num;
        num.push_back(0); // start with 0

        for (char c : input)
        {
            if (c < 33 || c > 126)
            {
                Warning("base94_decode: Invalid character encountered");
                return std::vector<unsigned char>();
            }
            unsigned int digit = c - 33;
            // Multiply 'num' by 94.
            int carry = 0;
            for (int i = num.size() - 1; i >= 0; i--)
            {
                int prod = num[i] * 94 + carry;
                num[i] = prod & 0xFF;
                carry = prod >> 8;
            }
            while (carry > 0)
            {
                num.insert(num.begin(), static_cast<unsigned char>(carry & 0xFF));
                carry >>= 8;
            }
            // Add the current digit.
            carry = digit;
            for (int i = num.size() - 1; i >= 0; i--)
            {
                int sum = num[i] + carry;
                num[i] = sum & 0xFF;
                carry = sum >> 8;
                if (carry == 0)
                    break;
            }
            while (carry > 0)
            {
                num.insert(num.begin(), static_cast<unsigned char>(carry & 0xFF));
                carry >>= 8;
            }
        }

        return num;
    }

    // ---------------------------------------------------------------------
    // Revised compactJWT() and expandJWT()
    // ---------------------------------------------------------------------
    //
    // The new approach:
    //  - For header and payload (which are always JSON), we simply decode the base64url
    //    input and then treat the result as plain text.
    //  - For the signature, we decode the base64url input then encode it using our
    //    new base94_encode(). To allow proper recovery of any leading zeros (lost during
    //    conversion) we also prefix the signature with its original byte–length.
    //  - The overall compact string is built in a simple textual format with length prefixes:
    //
    //       H<header_length>:<header>P<payload_length>:<payload>S<sig_length>:<base94_encoded_signature>
    //
    // When expanding the compact string, we parse out each field (using the given lengths),
    // decode the base94 signature (padding with leading zero bytes if necessary), and then
    // base64url–encode each of the three parts to reassemble a standard JWT string.

    inline std::string compactJWT(const std::string& jwt)
    {
        // Split the JWT string on dots.
        std::vector<std::string> parts;
        std::istringstream iss(jwt);
        std::string token;
        while (std::getline(iss, token, '.'))
        {
            parts.push_back(token);
        }
        if (parts.size() != 3)
        {
            Warning("compactJWT: JWT must have 3 parts");
            return "";
        }

        // Decode each part from base64url.
        std::vector<unsigned char> headerBin = base64url_decode(parts[0]);
        std::vector<unsigned char> payloadBin = base64url_decode(parts[1]);
        std::vector<unsigned char> signatureBin = base64url_decode(parts[2]);
        if (headerBin.empty() || payloadBin.empty() || signatureBin.empty())
        {
            Warning("compactJWT: base64url decoding failed");
            return "";
        }

        // Header and payload are JSON; convert to string directly.
        std::string header(headerBin.begin(), headerBin.end());
        std::string payload(payloadBin.begin(), payloadBin.end());

        // Encode the signature using our new base94 encoding.
        std::string encodedSignature = base94_encode(signatureBin);

        // Build the compact representation with length prefixes.
        // Format: H<header_length>:<header>P<payload_length>:<payload>S<sig_length>:<encoded_signature>
        std::string compact;
        compact.append("H");
        compact.append(std::to_string(header.size()));
        compact.push_back(':');
        compact.append(header);
        compact.append("P");
        compact.append(std::to_string(payload.size()));
        compact.push_back(':');
        compact.append(payload);
        compact.append("S");
        compact.append(std::to_string(signatureBin.size()));
        compact.push_back(':');
        compact.append(encodedSignature);

        return compact;
    }

    inline std::string expandJWT(const std::string& compact)
    {
        size_t pos = 0;

        // Parse header.
        if (compact.empty() || compact[pos] != 'H')
        {
            Warning("expandJWT: Invalid format, missing 'H'");
            return "";
        }
        pos++; // skip 'H'
        size_t colonPos = compact.find(':', pos);
        if (colonPos == std::string::npos)
        {
            Warning("expandJWT: Invalid format, missing colon after header length");
            return "";
        }
        size_t headerLen = std::stoul(compact.substr(pos, colonPos - pos));
        pos = colonPos + 1;
        if (pos + headerLen > compact.size())
        {
            Warning("expandJWT: Header length exceeds compact string size");
            return "";
        }
        std::string header = compact.substr(pos, headerLen);
        pos += headerLen;

        // Parse payload.
        if (pos >= compact.size() || compact[pos] != 'P')
        {
            Warning("expandJWT: Expected 'P' after header");
            return "";
        }
        pos++; // skip 'P'
        colonPos = compact.find(':', pos);
        if (colonPos == std::string::npos)
        {
            Warning("expandJWT: Invalid format, missing colon after payload length");
            return "";
        }
        size_t payloadLen = std::stoul(compact.substr(pos, colonPos - pos));
        pos = colonPos + 1;
        if (pos + payloadLen > compact.size())
        {
            Warning("expandJWT: Payload length exceeds compact string size");
            return "";
        }
        std::string payload = compact.substr(pos, payloadLen);
        pos += payloadLen;

        // Parse signature.
        if (pos >= compact.size() || compact[pos] != 'S')
        {
            Warning("expandJWT: Expected 'S' after payload");
            return "";
        }
        pos++; // skip 'S'
        colonPos = compact.find(':', pos);
        if (colonPos == std::string::npos)
        {
            Warning("expandJWT: Invalid format, missing colon after signature length");
            return "";
        }
        size_t sigLen = std::stoul(compact.substr(pos, colonPos - pos));
        pos = colonPos + 1;
        if (pos > compact.size())
        {
            Warning("expandJWT: No signature data found");
            return "";
        }
        std::string encodedSignature = compact.substr(pos);

        // Decode the signature using base94.
        std::vector<unsigned char> signatureBin = base94_decode(encodedSignature);
        // Because leading zero bytes are lost in the conversion, pad with zeros if needed.
        if (signatureBin.size() < sigLen)
        {
            std::vector<unsigned char> padded(sigLen - signatureBin.size(), 0);
            padded.insert(padded.end(), signatureBin.begin(), signatureBin.end());
            signatureBin = padded;
        }
        else if (signatureBin.size() > sigLen)
        {
            signatureBin.resize(sigLen);
        }

        // Re–encode each part to base64url.
        std::vector<unsigned char> headerBin(header.begin(), header.end());
        std::vector<unsigned char> payloadBin(payload.begin(), payload.end());
        std::string headerB64 = base64url_encode(headerBin);
        std::string payloadB64 = base64url_encode(payloadBin);
        std::string signatureB64 = base64url_encode(signatureBin);

        return headerB64 + "." + payloadB64 + "." + signatureB64;
    }

} // namespace jwt_compact

#endif // JWT_COMPACT_H
