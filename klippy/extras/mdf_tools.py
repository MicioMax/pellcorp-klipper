import importlib.util
import os
import traceback

LIVE_PATH = "/usr/data/printer_data/config/mdf_tools_live.py"

class MDFTools:
    def __init__(self, config):
        self.printer = config.get_printer()
        self.gcode = self.printer.lookup_object("gcode")
        self.live = None

        self.gcode.register_command(
            "MDF_RELOAD", self.cmd_MDF_RELOAD,
            desc="Reload MDF live tools"
        )

        self._load_live()

    def _load_live(self):
        if self.live and hasattr(self.live, "unload"):
            self.live.unload()

        spec = importlib.util.spec_from_file_location("mdf_tools_live", LIVE_PATH)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        self.live = mod.load(self.printer)

    def cmd_MDF_RELOAD(self, gcmd):
        try:
            self._load_live()
            gcmd.respond_info("MDF live tools reloaded")
        except Exception:
            gcmd.respond_info("MDF reload failed:\n%s" % traceback.format_exc())

def load_config(config):
    return MDFTools(config)
