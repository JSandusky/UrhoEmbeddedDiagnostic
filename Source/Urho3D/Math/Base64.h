/*
Copyright (C) 2013 Tomas Kislan
Copyright (C) 2013 Adam Rudd

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

// JSandusky: ported to Urho3D::String

#pragma once

#include <Urho3D/Container/Str.h>

const char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";

class Base64 {
public:
    static bool Encode(const Urho3D::String &in, Urho3D::String* out) {
        int i = 0, j = 0;
        size_t enc_len = 0;
        unsigned char a3[3];
        unsigned char a4[4];

        out->Resize(EncodedLength(in));

        int input_len = in.Length();
        Urho3D::String::ConstIterator input = in.Begin();

        while (input_len--) {
            a3[i++] = *(input++);
            if (i == 3) {
                a3_to_a4(a4, a3);

                for (i = 0; i < 4; i++) {
                    (*out)[enc_len++] = kBase64Alphabet[a4[i]];
                }

                i = 0;
            }
        }

        if (i) {
            for (j = i; j < 3; j++) {
                a3[j] = '\0';
            }

            a3_to_a4(a4, a3);

            for (j = 0; j < i + 1; j++) {
                (*out)[enc_len++] = kBase64Alphabet[a4[j]];
            }

            while ((i++ < 3)) {
                (*out)[enc_len++] = '=';
            }
        }

        return (enc_len == out->Length());
    }

    static bool Encode(const char *input, size_t input_length, char *out, size_t out_length) {
        int i = 0, j = 0;
        char *out_begin = out;
        unsigned char a3[3];
        unsigned char a4[4];

        size_t encoded_length = EncodedLength(input_length);

        if (out_length < encoded_length) return false;

        while (input_length--) {
            a3[i++] = *input++;
            if (i == 3) {
                a3_to_a4(a4, a3);

                for (i = 0; i < 4; i++) {
                    *out++ = kBase64Alphabet[a4[i]];
                }

                i = 0;
            }
        }

        if (i) {
            for (j = i; j < 3; j++) {
                a3[j] = '\0';
            }

            a3_to_a4(a4, a3);

            for (j = 0; j < i + 1; j++) {
                *out++ = kBase64Alphabet[a4[j]];
            }

            while ((i++ < 3)) {
                *out++ = '=';
            }
        }

        return (out == (out_begin + encoded_length));
    }

    static bool Decode(const Urho3D::String &in, Urho3D::String* out) {
        int i = 0, j = 0;
        size_t dec_len = 0;
        unsigned char a3[3];
        unsigned char a4[4];

        int input_len = in.Length();
        Urho3D::String::ConstIterator input = in.Begin();

        out->Resize(DecodedLength(in));

        while (input_len--) {
            if (*input == '=') {
                break;
            }

            a4[i++] = *(input++);
            if (i == 4) {
                for (i = 0; i <4; i++) {
                    a4[i] = b64_lookup(a4[i]);
                }

                a4_to_a3(a3, a4);

                for (i = 0; i < 3; i++) {
                    (*out)[dec_len++] = a3[i];
                }

                i = 0;
            }
        }

        if (i) {
            for (j = i; j < 4; j++) {
                a4[j] = '\0';
            }

            for (j = 0; j < 4; j++) {
                a4[j] = b64_lookup(a4[j]);
            }

            a4_to_a3(a3, a4);

            for (j = 0; j < i - 1; j++) {
                (*out)[dec_len++] = a3[j];
            }
        }

        return (dec_len == out->Length());
    }

    static bool Decode(const char *input, size_t input_length, char *out, size_t out_length) {
        int i = 0, j = 0;
        char *out_begin = out;
        unsigned char a3[3];
        unsigned char a4[4];

        size_t decoded_length = DecodedLength(input, input_length);

        if (out_length < decoded_length) return false;

        while (input_length--) {
            if (*input == '=') {
                break;
            }

            a4[i++] = *(input++);
            if (i == 4) {
                for (i = 0; i <4; i++) {
                    a4[i] = b64_lookup(a4[i]);
                }

                a4_to_a3(a3, a4);

                for (i = 0; i < 3; i++) {
                    *out++ = a3[i];
                }

                i = 0;
            }
        }

        if (i) {
            for (j = i; j < 4; j++) {
                a4[j] = '\0';
            }

            for (j = 0; j < 4; j++) {
                a4[j] = b64_lookup(a4[j]);
            }

            a4_to_a3(a3, a4);

            for (j = 0; j < i - 1; j++) {
                *out++ = a3[j];
            }
        }

        return (out == (out_begin + decoded_length));
    }

    static int DecodedLength(const char *in, size_t in_length) {
        int numEq = 0;

        const char *in_end = in + in_length;
        while (*--in_end == '=') ++numEq;

        return ((6 * in_length) / 8) - numEq;
    }

    static int DecodedLength(const Urho3D::String &in) {
        int numEq = 0;
        int n = in.Length();

        for (int i = n - 1; i >= 0; --i)
        {
            if (in[i] == '=')
                ++numEq;
            else
                break;
        }

        return ((6 * n) / 8) - numEq;
    }

    inline static int EncodedLength(size_t length) {
        return (length + 2 - ((length + 2) % 3)) / 3 * 4;
    }

    inline static int EncodedLength(const Urho3D::String &in) {
        return EncodedLength(in.Length());
    }

    inline static void StripPadding(Urho3D::String *in) {
        while (!in->Empty() && in->Back() == '=') in->Resize(in->Length() - 1);
    }

private:
    static inline void a3_to_a4(unsigned char * a4, unsigned char * a3) {
        a4[0] = (a3[0] & 0xfc) >> 2;
        a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
        a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
        a4[3] = (a3[2] & 0x3f);
    }

    static inline void a4_to_a3(unsigned char * a3, unsigned char * a4) {
        a3[0] = (a4[0] << 2) + ((a4[1] & 0x30) >> 4);
        a3[1] = ((a4[1] & 0xf) << 4) + ((a4[2] & 0x3c) >> 2);
        a3[2] = ((a4[2] & 0x3) << 6) + a4[3];
    }

    static inline unsigned char b64_lookup(unsigned char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 71;
        if (c >= '0' && c <= '9') return c + 4;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return 255;
    }
};
