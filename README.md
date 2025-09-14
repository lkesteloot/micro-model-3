# Micro Model III

Code for a miniature TRS-80 Model III, running on a Raspberry Pi Pico.

[Full write-up](https://www.teamten.com/lawrence/projects/micro-model-3/)

# Build and deploy

Instructions for macOS:

```
brew install --cask gcc-arm-embedded
(mkdir build && cd build && cmake ..)
cmake --build build --parallel
src/tools/PROGRAM
```

# License

Copyright &copy; Lawrence Kesteloot, [MIT license](LICENSE).
