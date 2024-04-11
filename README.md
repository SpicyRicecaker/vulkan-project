## Dependencices (MacOS)

Install the Vulkan SDK from LunarG. Ensure that the "Set up the runtime environment" settings are followed on the [LungarG starting guide](https://vulkan.lunarg.com/doc/sdk/latest/mac/getting_started.html), such that the `VK_ICD_FILENAMES` variable is set.

Ensure that the `homebrew` package manager is installed.

```shell
brew install glfw glm
```

## Compiling & Running (MacOS)

```shell
mkdir build && cd build # in root dir
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 .. && make -j && ./vulkan_project
```

## Compiling & Running (Windows)

Install `vcpkg`.