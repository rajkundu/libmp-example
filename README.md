# LibMP Example Usage Application

- See [LibMP - MediaPipe as a Shared Library](https://github.com/rajkundu/mediapipe) for more information on LibMP
- This example application reads an input image, runs MediaPipe Face Mesh on it, and:
    1) Gets the rendered MediaPipe output image with MediaPipe's annotations drawn on it (writes image to `mp_output.jpg`)
    2) Retrieves the raw face landmark coordinates and uses them to manually annotate the image (writes image to `manual_output.jpg`)
- Images are read from/written to disk using the single-header STB Image libraries
- Compilation is managed using CMake - see [`CMakeLists.txt`](/CMakeLists.txt) and [`libprotobuf.cmake`](/libprotobuf.cmake)
    - Change `${MEDIAPIPE_DIR}` in `CMakeLists.txt` to match your installation of LibMP
- Other MP example graphs should be supported, though they I haven't yet tested them. As described below, any of MediaPipe's many Protobuf-based data types should work with LibMP.

## How it Works
Some (most?) MediaPipe data types are based on [Protobuf](https://developers.google.com/protocol-buffers) messages. Thus, their data can be easily serialized and deserialized. LibMP provides functions which accomplish this, and this project imports both Protobuf & the Protobuf-based MediaPipe types - allowing for practically native use of MediaPipe data types in an external C++ application.

## Contributions
Questions, suggestions, issues, pull requests, etc. are all welcome!
