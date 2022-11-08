#include <iostream>
#include <chrono>
#include <memory>
#include <array>

// MediaPipe LibMP Header
#include "libmp.h"

// Compiled protobuf headers for MediaPipe types used
// (only landmark.pb.h is needed for this particular example)
#include "mediapipe/framework/formats/landmark.pb.h"

// single-header libraries for simple image I/O
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main(int argc, char* argv[]){
	const char* graph = R"(
		# from https://github.com/google/mediapipe/blob/master/mediapipe/graphs/face_mesh/face_mesh_desktop_live.pbtxt

		# MediaPipe graph that performs face mesh with TensorFlow Lite on CPU.

		# Input image. (ImageFrame)
		input_stream: "input_video"

		# Output image with rendered results. (ImageFrame)
		output_stream: "output_video"
		# Collection of detected/processed faces, each represented as a list of
		# landmarks. (std::vector<NormalizedLandmarkList>)
		output_stream: "multi_face_landmarks"

		# Throttles the images flowing downstream for flow control. It passes through
		# the very first incoming image unaltered, and waits for downstream nodes
		# (calculators and subgraphs) in the graph to finish their tasks before it
		# passes through another image. All images that come in while waiting are
		# dropped, limiting the number of in-flight images in most part of the graph to
		# 1. This prevents the downstream nodes from queuing up incoming images and data
		# excessively, which leads to increased latency and memory usage, unwanted in
		# real-time mobile applications. It also eliminates unnecessarily computation,
		# e.g., the output produced by a node may get dropped downstream if the
		# subsequent nodes are still busy processing previous inputs.
		node {
			calculator: "FlowLimiterCalculator"
			input_stream: "input_video"
			input_stream: "FINISHED:output_video"
			input_stream_info: {
				tag_index: "FINISHED"
				back_edge: true
			}
			output_stream: "throttled_input_video"
		}

		# Defines side packets for further use in the graph.
		node {
			calculator: "ConstantSidePacketCalculator"
			output_side_packet: "PACKET:0:num_faces"
			output_side_packet: "PACKET:1:with_attention"
			node_options: {
				[type.googleapis.com/mediapipe.ConstantSidePacketCalculatorOptions]: {
					packet { int_value: 1 }
					packet { bool_value: true }
				}
			}
		}
		# Subgraph that detects faces and corresponding landmarks.
		node {
			calculator: "FaceLandmarkFrontCpu"
			input_stream: "IMAGE:throttled_input_video"
			input_side_packet: "NUM_FACES:num_faces"
			input_side_packet: "WITH_ATTENTION:with_attention"
			output_stream: "LANDMARKS:multi_face_landmarks"
			output_stream: "ROIS_FROM_LANDMARKS:face_rects_from_landmarks"
			output_stream: "DETECTIONS:face_detections"
			output_stream: "ROIS_FROM_DETECTIONS:face_rects_from_detections"
		}
		# Subgraph that renders face-landmark annotation onto the input image.
		node {
			calculator: "FaceRendererCpu"
			input_stream: "IMAGE:throttled_input_video"
			input_stream: "LANDMARKS:multi_face_landmarks"
			input_stream: "NORM_RECTS:face_rects_from_landmarks"
			input_stream: "DETECTIONS:face_detections"
			output_stream: "IMAGE:output_video"
		}
	)";

	// Create mp face mesh
	std::shared_ptr<mediapipe::LibMP> face_mesh(mediapipe::LibMP::Create(graph, "input_video"));

	face_mesh->AddOutputStream("output_video");
	face_mesh->AddOutputStream("multi_face_landmarks");
    face_mesh->Start();

	// Read in input image
	std::string img_path("input.jpg");
	int img_width, img_height, img_channels;
    uint8_t* img = stbi_load(img_path.c_str(), &img_width, &img_height, &img_channels, 0);
    if (img == nullptr){
        std::cerr << "Error loading image file '" << img_path << "'" << std::endl;
        return -1;
    }
	std::cout << "Input image: " << img_path << std::endl;
    std::cout << "\tw:\t" << img_width << std::endl;
    std::cout << "\th:\t" << img_height << std::endl;
    std::cout << "\tc:\t" << img_channels << std::endl;

	// Feed input image into graph
	if(!face_mesh->Process(img, img_width, img_height)){
		std::cerr << "Process() failed!" << std::endl;
		return 1;
	}

	// ----- MP Output Image ----- //

	// Get output image packet
	const void* img_packet = face_mesh->GetOutputPacket("output_video");

	// Get output image size, create copy buffer, and fill it
	size_t imgsize = mediapipe::LibMP::GetOutputImageSize(img_packet);
	std::shared_ptr<uint8_t[]> mp_out(new uint8_t[imgsize]);
	if(!mediapipe::LibMP::WriteOutputImage(mp_out.get(), img_packet)){
		std::cerr << "WriteOutputImage failed!" << std::endl;
		return 1;
	}

	// Write MP output image to disk
	stbi_write_jpg("mp_output.jpg", img_width, img_height, img_channels, mp_out.get(), 100);
	std::cout << "Wrote MP output image to disk" << std::endl;

	// ----- MP Landmarks ----- //

	// Get MP landmark packet
	std::vector<mediapipe::NormalizedLandmarkList> lm_vec;
	const void* lm_packet = face_mesh->GetOutputPacket("multi_face_landmarks");
	size_t num_faces = mediapipe::LibMP::GetProtoMsgVecSize(lm_packet);

	// Create multi_face_landmarks from packet's protobuf data
	std::vector<mediapipe::NormalizedLandmarkList> multi_face_landmarks;
	for (int face_num = 0; face_num < num_faces; face_num++){
		// Get reference to protobuf message for face
		const void* lm_list_proto = mediapipe::LibMP::GetPacketProtoMsgAt(lm_packet, face_num);
		// Get byte size of protobuf message
		size_t lm_list_proto_size = mediapipe::LibMP::GetProtoMsgByteSize(lm_list_proto);

		// Create buffer to hold protobuf message data; copy data to buffer
		std::shared_ptr<uint8_t[]> proto_data(new uint8_t[lm_list_proto_size]);
		mediapipe::LibMP::WriteProtoMsgData(proto_data.get(), lm_list_proto, static_cast<int>(lm_list_proto_size));

		// Initialize a mediapipe::NormalizedLandmarkList object from the buffer
		multi_face_landmarks.emplace_back();
		multi_face_landmarks[face_num].ParseFromArray(proto_data.get(), static_cast<int>(lm_list_proto_size));
	}

	// Create vector of vectors of arrays for landmarks
	// dimensions = [face_num][landmark_num][x/y/z]
	// e.g. normalized_landmarks[0][37][1] = face 0 -> landmark 37 -> y-value
	std::vector<std::vector<std::array<float, 3>>> normalized_landmarks;
	for (int face_num = 0; face_num < num_faces; face_num++){
		normalized_landmarks.emplace_back();
		for (const mediapipe::NormalizedLandmark& lm : multi_face_landmarks[face_num].landmark()){
			normalized_landmarks[face_num].push_back({lm.x(), lm.y(), lm.z()});
		}
	}

	// Manually iterate over landmarks and draw them on the image as green squares
	int square_size = 4;
	uint8_t* buffer = img;
	for (int face_num = 0; face_num < num_faces; face_num++){
		for (const std::array<float, 3>& norm_xyz : normalized_landmarks[face_num]){
			int x = static_cast<int>(norm_xyz[0] * img_width);
			int y = static_cast<int>(norm_xyz[1] * img_height);
			int x1 = x - square_size / 2;
			int x2 = x + (square_size + 1) / 2;
			int y1 = y - square_size / 2;
			int y2 = y + (square_size + 1) / 2;

			// Clamp x1, y1, x2, y2 to image bounds
			x1 = x1 < 0 ? 0 : x1;
			y1 = y1 < 0 ? 0 : y1;
			x2 = x2 > img_width ? img_width : x2;
			y2 = y2 > img_height ? img_height : y2;

			for (int v = y1; v < y2; v++){
				for (int u = x1; u < x2; u++){
					unsigned int idx = img_channels * (v * img_width + u);
					buffer[idx+0] = 0; // Red
					buffer[idx+1] = 255; // Green
					buffer[idx+2] = 0; // Blue
				}
			}
		}
	}

	// Write image with manual drawing to disk
	stbi_write_jpg("manual_output.jpg", img_width, img_height, img_channels, buffer, 100);
	std::cout << "Wrote manual output image to disk" << std::endl;

	// Wait for user to press enter to end program
	std::string s;
	std::cout << "Press enter to continue... ";
	std::getline(std::cin, s);

	return 0;
}
