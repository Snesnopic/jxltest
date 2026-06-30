#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <stdexcept>
#include <jxl/encode.h>
#include <jxl/decode.h>

bool read_file(const std::string& path, std::vector<uint8_t>& buf) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    auto size = f.tellg();
    f.seekg(0, std::ios::beg);
    buf.resize(size);
    return !!f.read(reinterpret_cast<char*>(buf.data()), size);
}

bool write_file(const std::string& path, const std::vector<uint8_t>& buf) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    return !!f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
}

size_t get_bytes_per_channel(const JxlDataType data_type) {
    if (data_type == JXL_TYPE_FLOAT) return 4;
    if (data_type == JXL_TYPE_UINT16) return 2;
    return 1;
}

void recompress_jxl(const std::string& input, const std::string& output) {
    std::vector<uint8_t> input_buf;
    if (!read_file(input, input_buf)) throw std::runtime_error("Cannot read input");

    JxlDecoder* dec = JxlDecoderCreate(nullptr);
    if (!dec) throw std::runtime_error("Cannot create decoder");

    JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE | JXL_DEC_BOX);
    JxlDecoderSetInput(dec, input_buf.data(), input_buf.size());
    JxlDecoderCloseInput(dec);

    JxlBasicInfo info{};
    bool ok = true;

    struct FrameData {
        std::vector<uint8_t> pixels;
        JxlFrameHeader header{};
    };
    std::vector<FrameData> frames;
    std::size_t stride = 0;
    JxlPixelFormat format = {};
    JxlDataType data_type = {};
    std::vector<std::pair<std::string, std::vector<uint8_t>>> metadata_boxes;

    for (;;) {
        const JxlDecoderStatus status = JxlDecoderProcessInput(dec);
        if (status == JXL_DEC_ERROR) { ok = false; break; }
        if (status == JXL_DEC_BASIC_INFO) {
            if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec, &info)) { ok = false; break; }
            uint32_t num_channels = info.num_color_channels;
            if (info.alpha_bits > 0) num_channels++;
            
            if (info.exponent_bits_per_sample > 0) data_type = JXL_TYPE_FLOAT;
            else if (info.bits_per_sample > 8) data_type = JXL_TYPE_UINT16;
            else data_type = JXL_TYPE_UINT8;

            format = {num_channels, data_type, JXL_NATIVE_ENDIAN, 0};
            stride = info.xsize * num_channels * get_bytes_per_channel(data_type);
        }
        if (status == JXL_DEC_FRAME) {
            JxlFrameHeader header;
            if (JXL_DEC_SUCCESS != JxlDecoderGetFrameHeader(dec, &header)) { ok = false; break; }
            FrameData frame;
            frame.header = header;
            frame.pixels.resize(stride * info.ysize);
            if (JXL_DEC_SUCCESS != JxlDecoderSetImageOutBuffer(dec, &format, frame.pixels.data(), frame.pixels.size())) { ok = false; break; }
            frames.push_back(std::move(frame));
        } else if (status == JXL_DEC_BOX) {
            JxlBoxType type;
            if (JXL_DEC_SUCCESS != JxlDecoderGetBoxType(dec, type, JXL_FALSE)) { ok = false; break; }
            uint64_t box_size;
            if (JXL_DEC_SUCCESS != JxlDecoderGetBoxSizeContents(dec, &box_size)) { ok = false; break; }
            std::vector<uint8_t> box_data(box_size);
            if (JXL_DEC_SUCCESS != JxlDecoderSetBoxBuffer(dec, box_data.data(), box_data.size())) { ok = false; break; }
            if (JXL_DEC_SUCCESS != JxlDecoderProcessInput(dec)) { ok = false; break; }
            metadata_boxes.emplace_back(std::pair(std::string(type,4), std::move(box_data)));
        }
        if (status == JXL_DEC_SUCCESS) break;
    }
    JxlDecoderDestroy(dec);
    if (!ok) throw std::runtime_error("Decode failed");

    JxlEncoder* enc = JxlEncoderCreate(nullptr);
    if (!enc) throw std::runtime_error("Cannot create encoder");
    if (JXL_ENC_SUCCESS != JxlEncoderSetBasicInfo(enc, &info)) { JxlEncoderDestroy(enc); throw std::runtime_error("Basic info failed"); }
    
    JxlEncoderStoreJPEGMetadata(enc, JXL_TRUE);
    for (const auto& box : metadata_boxes) {
        JxlEncoderAddBox(enc, box.first.c_str(), box.second.data(), box.second.size(), JXL_FALSE);
    }

    for (const auto &frame : frames) {
        JxlEncoderFrameSettings* frame_settings = JxlEncoderFrameSettingsCreate(enc, nullptr);
        JxlEncoderSetFrameLossless(frame_settings, JXL_TRUE);
        JxlEncoderFrameSettingsSetOption(frame_settings, JXL_ENC_FRAME_SETTING_EFFORT, 9);
        if (JXL_ENC_SUCCESS != JxlEncoderAddImageFrame(frame_settings, &format, frame.pixels.data(), frame.pixels.size())) {
            JxlEncoderDestroy(enc); throw std::runtime_error("Add frame failed");
        }
    }
    JxlEncoderCloseInput(enc);

    std::vector<uint8_t> out_buf(1 << 20);
    uint8_t* next_out = out_buf.data();
    std::size_t avail_out = out_buf.size();
    JxlEncoderStatus enc_status;
    while ((enc_status = JxlEncoderProcessOutput(enc, &next_out, &avail_out)) == JXL_ENC_NEED_MORE_OUTPUT) {
        std::size_t offset = next_out - out_buf.data();
        out_buf.resize(out_buf.size() * 2);
        next_out = out_buf.data() + offset;
        avail_out = out_buf.size() - offset;
    }
    if (enc_status != JXL_ENC_SUCCESS) { JxlEncoderDestroy(enc); throw std::runtime_error("Encode failed"); }
    
    std::size_t out_size = next_out - out_buf.data();
    out_buf.resize(out_size);
    if (!write_file(output, out_buf)) { JxlEncoderDestroy(enc); throw std::runtime_error("Cannot write output"); }
    
    JxlEncoderDestroy(enc);
    std::cout << "Successfully recompressed " << input << " to " << output << "\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: jxltest <input.jxl> <output.jxl>\n";
        return 1;
    }
    try {
        recompress_jxl(argv[1], argv[2]);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
