#include "runtime_config.h"

#include <iostream>
#include <sstream>

namespace {
bool Require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}
} // namespace

int main() {
    {
        std::istringstream input("[video]\nmetalfx_spatial_upscaling = true\n");
        const RuntimeUserConfig config = RuntimeConfigFile::ParseConfig(input, "enabled.toml");
        if (!Require(config.metalFxSpatialUpscaling == std::optional<bool>{true},
                     "valid MetalFX setting was not loaded")) return 1;
    }
    {
        std::istringstream input("[video]\nmetalfx_spatial_upscaling = false\n");
        const RuntimeUserConfig config = RuntimeConfigFile::ParseConfig(input, "disabled.toml");
        if (!Require(config.metalFxSpatialUpscaling == std::optional<bool>{false},
                     "false MetalFX setting was not loaded")) return 1;
    }
    {
        std::istringstream input("[video]\nmetalfx_spatial_upscaling = \"yes\"\n");
        const RuntimeUserConfig config = RuntimeConfigFile::ParseConfig(input, "invalid.toml");
        if (!Require(!config.metalFxSpatialUpscaling, "invalid MetalFX setting was accepted")) return 1;
    }
    {
        std::istringstream input("[netplay]\nhost_port = 6000\n");
        const RuntimeUserConfig config = RuntimeConfigFile::ParseConfig(input, "netplay-port.toml");
        if (!Require(config.netplayHostPort == std::optional<uint32_t>{6000},
                     "valid netplay host port was not loaded")) return 1;
    }
    {
        std::istringstream input("[netplay]\nhost_port = 65535\n");
        const RuntimeUserConfig config = RuntimeConfigFile::ParseConfig(input, "netplay-port-max.toml");
        if (!Require(config.netplayHostPort == std::optional<uint32_t>{65535},
                     "highest netplay host port was not loaded")) return 1;
    }
    for (const char* text : {"[netplay]\nhost_port = 80\n", "[netplay]\nhost_port = 70000\n",
                             "[netplay]\nhost_port = -1\n", "[netplay]\nhost_port = \"6000\"\n",
                             "[video]\nhost_port = 6000\n"}) {
        std::istringstream input(text);
        const RuntimeUserConfig config = RuntimeConfigFile::ParseConfig(input, "netplay-port-invalid.toml");
        if (!Require(!config.netplayHostPort, "invalid netplay host port was accepted")) return 1;
    }
    return 0;
}
