/* Copyright 2012 Google Inc. All Rights Reserved. */

//#include "sw_util.h"
//#include "sw_stream.h"
//#include "sw_debug.h"
#include "vp9hwd_stream.h"

u32 SwGetBits(struct StrmData *stream, u32 num_bits)
{
    u32 out;
    //ASSERT(stream);
   // ASSERT(num_bits < 32);
    if(num_bits == 0)
        return 0;

    out = SwShowBits(stream, 32) >> (32 - num_bits);
    if(SwFlushBits(stream, num_bits) == HANTRO_OK)
    {
        return (out);
    }
    else
    {
        return (END_OF_STREAM);
    }
}

u32 SwGetBitsUnsignedMax(struct StrmData *stream, u32 max_value)
{
    s32 bits = 0;
    u32 num_values = max_value;
    u32 value;

    /* Bits for unsigned value */
    if(num_values > 1)
    {
        num_values--;
        while(num_values > 0)
        {
            bits++;
            num_values >>= 1;
        }
    }

    value = SwGetBits(stream, bits);
    return (value > max_value) ? max_value : value;
}

u32 SwShowBits(const struct StrmData *stream, u32 num_bits)
{
    s32 bits;
    u32 out, out_bits;
    u32 tmp_read_bits;
    const u8 *strm;
    //ASSERT(stream);
    //ASSERT(stream->strm_curr_pos);
    //ASSERT(stream->bit_pos_in_word < 8);
    //ASSERT(stream->bit_pos_in_word == (stream->strm_buff_read_bits & 0x7));
    //ASSERT(num_bits <= 32);

    strm = stream->strm_curr_pos;

    /* bits left in the buffer */
    bits = (s32)stream->strm_buff_size * 8 - (s32)stream->strm_buff_read_bits;
    if(!bits)
    {
        return (0);
    }

    if(!stream->remove_emul3_byte)
    {
        out = out_bits = 0;
        tmp_read_bits = stream->strm_buff_read_bits;
        if(stream->bit_pos_in_word)
        {
            out = strm[0] << (24 + stream->bit_pos_in_word);
            strm++;
            out_bits = 8 - stream->bit_pos_in_word;
            bits -= out_bits;
            tmp_read_bits += out_bits;
        }

        while(bits && out_bits < num_bits)
        {
            /* check emulation prevention byte */
            if(tmp_read_bits >= 16 && strm[-2] == 0x0 &&
                strm[-1] == 0x0 && strm[0] == 0x3)
            {
                strm++;
                tmp_read_bits += 8;
                bits -= 8;
                /* emulation prevention byte shall not be the last byte of the
                * stream */
                if(bits <= 0)
                    break;
            }
            tmp_read_bits += 8;
            if(out_bits <= 24)
                out |= (u32)(*strm++) << (24 - out_bits);
            else
                out |= (u32)(*strm++) >> (out_bits - 24);
            out_bits += 8;
            bits -= 8;
        }
        return (out >> (32 - num_bits));
    }
    else
    {
        u32 shift;
        /* at least 32-bits in the buffer */
        if(bits >= 32)
        {
            u32 bit_pos_in_word = stream->bit_pos_in_word;
            out = ((u32)strm[3]) | ((u32)strm[2] << 8) |
                ((u32)strm[1] << 16) | ((u32)strm[0] << 24);

            if(bit_pos_in_word)
            {
                out <<= bit_pos_in_word;
                out |= (u32)strm[4] >> (8 - bit_pos_in_word);
            }
            return (out >> (32 - num_bits));
        }
        /* at least one bit in the buffer */
        else if(bits > 0)
        {
            shift = (s32)(24 + stream->bit_pos_in_word);
            out = (u32)(*strm++) << shift;
            bits -= (s32)(8 - stream->bit_pos_in_word);
            while(bits > 0)
            {
                shift -= 8;
                out |= (u32)(*strm++) << shift;
                bits -= 8;
            }
            return (out >> (32 - num_bits));
        }
        else
        {
            return (0);
        }
    }
}

u32 SwFlushBits(struct StrmData *stream, u32 num_bits)
{
    u32 bytes_left;
    const u8 *strm;

    //ASSERT(stream);
    //ASSERT(stream->strm_buff_start);
    //ASSERT(stream->strm_curr_pos);
    //ASSERT(stream->bit_pos_in_word < 8);
    //ASSERT(stream->bit_pos_in_word == (stream->strm_buff_read_bits & 0x7));

    if(!stream->remove_emul3_byte)
    {
        if((stream->strm_buff_read_bits + num_bits) >
            (8 * stream->strm_buff_size))
        {
            stream->strm_buff_read_bits = 8 * stream->strm_buff_size;
            stream->bit_pos_in_word = 0;
            stream->strm_curr_pos = stream->strm_buff_start + stream->strm_buff_size;
            return (END_OF_STREAM);
        }
        else
        {
            bytes_left =
                (8 * stream->strm_buff_size-stream->strm_buff_read_bits) / 8;
            strm = stream->strm_curr_pos;
            if(stream->bit_pos_in_word)
            {
                if(num_bits < 8 - stream->bit_pos_in_word)
                {
                    stream->strm_buff_read_bits += num_bits;
                    stream->bit_pos_in_word += num_bits;
                    return (HANTRO_OK);
                }
                num_bits -= 8 - stream->bit_pos_in_word;
                stream->strm_buff_read_bits += 8 - stream->bit_pos_in_word;
                stream->bit_pos_in_word = 0;
                strm++;

                if(stream->strm_buff_read_bits>=16 && bytes_left &&
                    strm[-2]==0x0 && strm[-1]==0x0 && strm[0]==0x3)
                {
                    strm++;
                    stream->strm_buff_read_bits += 8;
                    bytes_left--;
                    stream->emul_byte_count++;
                }
            }

            while(num_bits >= 8 && bytes_left)
            {
                if(bytes_left > 2 && strm[0] == 0 && strm[1] == 0 && strm[2] <= 1)
                {
                    /* trying to flush part of start code prefix -> error */
                    return (HANTRO_NOK);
                }
                strm++;
                stream->strm_buff_read_bits += 8;
                bytes_left--;

                /* check emulation prevention byte */

                if(stream->strm_buff_read_bits >= 16 && bytes_left &&
                    strm[-2] == 0x0 && strm[-1] == 0x0 && strm[0] == 0x3)
                {
                    strm++;
                    stream->strm_buff_read_bits += 8;
                    bytes_left--;
                    stream->emul_byte_count++;
                }
                num_bits -= 8;
            }

            if(num_bits && bytes_left)
            {
                if(bytes_left > 2 && strm[0] == 0 && strm[1] == 0 && strm[2] <= 1)
                {
                    /* trying to flush part of start code prefix -> error */
                    return (HANTRO_NOK);
                }

                stream->strm_buff_read_bits += num_bits;
                stream->bit_pos_in_word = num_bits;
                num_bits = 0;
            }

            stream->strm_curr_pos = strm;
            if(num_bits)
                return (END_OF_STREAM);
            else
                return (HANTRO_OK);
        }
    }
    else
    {
        stream->strm_buff_read_bits += num_bits;
        stream->bit_pos_in_word = stream->strm_buff_read_bits & 0x7;
        if((stream->strm_buff_read_bits) <= (8 * stream->strm_buff_size))
        {
            stream->strm_curr_pos =
                stream->strm_buff_start + (stream->strm_buff_read_bits >> 3);
            return (HANTRO_OK);
        }
        else
            return (END_OF_STREAM);
    }
}

#if 0
u32 SwIsByteAligned(const struct StrmData *stream)
{
    if(!stream->bit_pos_in_word)
        return (HANTRO_TRUE);
    else
        return (HANTRO_FALSE);
}
#endif

