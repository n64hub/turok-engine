// RT64's render to RAM: whether each frame RT64 draws is also copied back into RDRAM, where the console's RDP leaves
// it. A game needs the copy only where its CPU reads a frame it drew (a menu's background, a screenshot, a depth test
// on the CPU) or writes into one, and none of the Turok games does (docs/render_to_ram.md). Without it RT64 skips a
// second draw of every frame at the game's resolution, and a browser no longer holds each display list 5-6 ms for the
// readback. It is still an option, Off by default, for something that looks or behaves wrong without it.

#include <cstdint>
#include <string>
#include <vector>

#include "recompui/config.h"
#include "recompui/renderer.h"

#include "turok_engine.h"

namespace {
    enum class CopyFramesToRam {
        Off,
        Original,
        OptionCount
    };
    const std::vector<recomp::config::ConfigOptionEnumOption> copy_frames_to_ram_options = {
        {CopyFramesToRam::Off, "Off", "Off"},
        {CopyFramesToRam::Original, "Original", "Original"},
    };
    const std::string copy_frames_to_ram_option = "copy_frames_to_ram";
}

void turok::add_copy_frames_option(recomp::config::Config& graphics) {
    // The render context reads the setting when it is made, at startup once the config has loaded, so a change takes
    // effect on the next start. The default is set here first: a first start has no graphics.json, and loading then
    // calls no callback, which would leave RT64's own default (on).
    recompui::renderer::set_render_to_ram(false);
    graphics.add_enum_option(
        copy_frames_to_ram_option,
        "Copy Frames to RAM",
        "Whether every frame is also copied back into the console's memory, as the N64 leaves it there for the game "
        "to read. The game doesn't read its frames back. The copy is a second draw of each frame, and in a browser it "
        "holds each frame back by about 6 ms."
        "<br/>"
        "<b>Off:</b> no copy. "
        "<b>Original:</b> every frame is copied, as on the console. Try it if something looks or behaves wrong."
        "<br/>"
        "Takes effect the next time the game is started.",
        copy_frames_to_ram_options,
        CopyFramesToRam::Off
    );
    // Called on load too, before the render context is made. The Graphics tab also calls it for a change it only
    // previews, which is left out: the setting acts on load and on Apply, as the stock options do.
    graphics.add_option_change_callback(copy_frames_to_ram_option,
        [](recomp::config::ConfigValueVariant cur_value, recomp::config::ConfigValueVariant,
           recomp::config::OptionChangeContext change_context) {
            if (change_context != recomp::config::OptionChangeContext::Temporary) {
                recompui::renderer::set_render_to_ram(
                    static_cast<CopyFramesToRam>(std::get<uint32_t>(cur_value)) == CopyFramesToRam::Original);
            }
        });
}
