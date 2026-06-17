# See https://docs.conan.io/2/reference/conanfile.html

import os
from pathlib import Path

import conan
import conan.tools.meson
import conan.tools.files

class NutrimaticConan(conan.ConanFile):
    name = "nutrimatic"
    version = "0.1"
    package_type = "application"

    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "openfst_prefix": ["ANY"],
        "mingw_prefix": ["ANY"],
    }
    default_options = {
        "shared": False,
        "openfst_prefix": "C:/msys64/usr/local",
        "mingw_prefix": "C:/msys64/mingw64",
    }

    requires = []
    tool_requires = ["meson/1.7.2", "ninja/1.13.2"]
    generators = ["MesonToolchain", "PkgConfigDeps", "VirtualBuildEnv"]

    exports_sources = "source/*"
    no_copy_source = True

    def validate(self):
        conan.tools.build.check_min_cppstd(self, 17)

    def layout(self):
        self.folders.source = "source"
        self.folders.build = "build"
        self.folders.generators = "build/dep-info"

    def generate(self):
        openfst_prefix = str(self.options.openfst_prefix).replace("\\", "/")
        openfst_pc = Path(self.generators_folder) / "openfst.pc"
        openfst_pc.write_text(
            f"""prefix={openfst_prefix}
exec_prefix=${{prefix}}
libdir=${{exec_prefix}}/lib
includedir=${{prefix}}/include

Name: OpenFst
Description: OpenFst finite-state transducer library
Version: 1.8.2
Libs: -L${{libdir}} -lfst
Cflags: -I${{includedir}}
""",
            encoding="utf-8",
        )

    def build(self):
        mingw_prefix = str(self.options.mingw_prefix).replace("\\", "/")
        mingw_bin = f"{mingw_prefix}/bin"
        os.environ.setdefault("PKG_CONFIG", f"{mingw_bin}/pkg-config.exe")
        os.environ["PATH"] = os.pathsep.join(
            [mingw_bin, os.environ.get("PATH", "")]
        )
        pkg_config_path = os.environ.get("PKG_CONFIG_PATH")
        os.environ["PKG_CONFIG_PATH"] = (
            self.generators_folder
            if not pkg_config_path
            else os.pathsep.join([self.generators_folder, pkg_config_path])
        )
        meson = conan.tools.meson.Meson(self)
        meson.configure(reconfigure=True)
        meson.build()

    def package(self):
        meson = conan.tools.meson.Meson(self)
        meson.install()
        source_root, output_dir = self.recipe_folder, self.package_folder
        conan.tools.files.copy(self, "cgi_scripts/*", source_root, output_dir)
        conan.tools.files.copy(self, "web_static/*", source_root, output_dir)
