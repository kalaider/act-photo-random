#pragma once

#include <cstdint>
#include <act-common/dialect.h>

namespace act_photo
{

    const std::uint8_t method_get = 0;
    const std::uint8_t method_set = 1;

    const std::uint8_t var_packet = 7;
    const std::uint8_t var_coefs  = 5;

    const std::uint8_t packet_size      = 11;
    const std::uint8_t packet_body_size = 9;
    const std::uint8_t packet_delimiter = 103; /* #53 */

    using packet_t = struct
    {
        std::uint8_t adc1, adc2;
        std::int16_t cur_err, int_err, pwm;
        std::uint8_t ocr2;
    };

    using desired_coefs_t = struct
    {
        double kp, ki, ks;
    };

    using coefs_t = struct
    {
        std::uint16_t kp_m; std::uint8_t kp_d;
        std::uint16_t ki_m; std::uint8_t ki_d;
        std::uint16_t ks_m; std::uint8_t ks_d;
    };

    using command_t = struct
    {
        using data_t = std::vector < std::uint8_t > ;

        std::uint8_t method;
        std::uint8_t var;
        data_t       bytes;
    };

    inline packet_t read_packet(char * input /* without headings */)
    {
        return {
            (std::uint8_t) ((std::uint8_t) input[0]),
            (std::uint8_t) ((std::uint8_t) input[1]),
            (std::int16_t) ((((std::uint8_t) input[2]) << 8) | ((std::uint8_t) input[3])),
            (std::int16_t) ((((std::uint8_t) input[4]) << 8) | ((std::uint8_t) input[5])),
            (std::int16_t) ((((std::uint8_t) input[6]) << 8) | ((std::uint8_t) input[7])),
            (std::uint8_t) ((std::uint8_t) input[8])
        };
    }
    
    /*
     * #53 <enchancement>
     *     Review the packet format.
     *     
     * The new packet format is < delimiter | data | checksum >,
     * where checksum is calculated as delimiter ^ adc1 ^ (cerr & 0xff) ^ ocr2.
     */
    inline char read_checksum(char * input /* with headings */)
    {
        return input[0] ^ input[1] ^ input[4] ^ input[9];
    }

    inline void serialize(const command_t &command, char * output)
    {
        output[0] = command.method;
        output[1] = command.var;
        memcpy_s(output,               command.bytes.size() + 2,
                 command.bytes.data(), command.bytes.size() + 2);
    }

    inline void calculate_optimal_coef
        (
        double desired, double &optimal, std::uint16_t &base, std::uint8_t &exponent
        )
    {
        double x0 = (int) desired, x;
        std::uint16_t y, y0 = x0;
        std::uint8_t i0 = 0;
        for (std::uint8_t i = 1; i < 15; i++)
        {
            y = (int) ((1 << i) * desired);
            x = ((double) y) / (1 << i);
            if ((abs(x - desired) < abs(x0 - desired))
                && (y < (std::numeric_limits<std::uint16_t>::max)() / 2))
            {
                x0 = x;
                y0 = y;
                i0 = i;
            }
        }
        optimal = x0;
        base = y0;
        exponent = i0;
    }

    inline coefs_t calculate_optimal_coefs(desired_coefs_t desired,
                                           desired_coefs_t &optimal)
    {
        coefs_t coefs;
        calculate_optimal_coef(desired.kp, optimal.kp, coefs.kp_m, coefs.kp_d);
        calculate_optimal_coef(desired.ki, optimal.ki, coefs.ki_m, coefs.ki_d);
        calculate_optimal_coef(desired.ks, optimal.ks, coefs.ks_m, coefs.ks_d);
        return coefs;
    }

    inline command_t set_coefs_command(coefs_t coefs)
    {
        command_t::data_t bytes(9);
        bytes[0] = (coefs.kp_m >> 8) & 0xff; bytes[1] = (coefs.kp_m & 0xff);
        bytes[2] = coefs.kp_d;
        bytes[3] = (coefs.ki_m >> 8) & 0xff; bytes[4] = (coefs.ki_m & 0xff);
        bytes[5] = coefs.ki_d;
        bytes[6] = (coefs.ks_m >> 8) & 0xff; bytes[7] = (coefs.ks_m & 0xff);
        bytes[8] = coefs.ks_d;
        return { method_set, var_coefs, bytes };
    }

    inline command_t set_coefs_command(desired_coefs_t coefs)
    {
        desired_coefs_t optimal;
        return set_coefs_command(calculate_optimal_coefs(coefs, optimal));
    }

    inline command_t get_packet_command()
    {
        return { method_get, var_packet, command_t::data_t() };
    }


    class dialect
        : public com_port_api::dialect < packet_t, command_t >
    {

    public:

        bool read(packet_t &dst, com_port_api::byte_buffer &src)
        {
            char c;

            for(;;)
            {
                if (!src.remaining())
                {
                    return false;
                }

                // don't use `get` to avoid subsequent call to `put`
                c = src.data()[0];

                if (c == act_photo::packet_delimiter)
                {
                    if ((src.remaining() >= act_photo::packet_size))
                    {
                        char checksum = src.data()[act_photo::packet_size - 1]; // last byte
                        char s = act_photo::read_checksum(src.data() /* with headings */);
                        if (checksum == s)
                        {
                            dst = read_packet(src.data() + 1 /* without headings */);

                            // move the buffer position
                            src.increase_position(act_photo::packet_size);

                            return true;
                        }
                        else
                        {
                            // try next byte - invalid packet
                            src.increase_position(1);
                        }
                    }
                    else
                    {
                        // insufficient bytes to read the complete packet
                        return false;
                    }
                }
                else
                {
                    // if not delimiter - skip and try next byte
                    src.increase_position(1);
                }
            }
            return false;
        }

        bool write(com_port_api::byte_buffer &dst, const command_t &src)
        {
            if (dst.remaining() < src.bytes.size() + 2)
            {
                // insufficient place in output buffer
                return false;
            }

            serialize(src, dst.data());

            dst.increase_position(src.bytes.size() + 2);

            return true;
        }
    };
}
