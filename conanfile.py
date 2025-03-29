from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, CMake, cmake_layout

class BitECS(ConanFile):
    name = "bitecs"
    version = "0.1"
    url = "https://github.com/cyanidle/bitecs"
    author = "Alexey Doronin"
    license = "MIT"

    settings = "os", "arch", "compiler", "build_type"

    exports_sources = "cmake/*", "CMakeLists.txt", "LICENSE", "src/*", "include/*"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }

    default_options = {
        "shared": False,
        "fPIC": True,
    }

    def requirements(self):
        self.test_requires("gtest/1.16.0")
        self.test_requires("benchmark/1.9.1")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.user_presets_path = ""
        tc.variables["CONAN_CALLED"] = True
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.components["core"].libs = ["bitecs-core"]
        self.cpp_info.requires = ["core"]