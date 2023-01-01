#include <iostream>
#include <string>
#include <chrono>
#include <memory>
#include <array>
#include <vector>

// LibMP Header
#include "libmp.h"

// Compiled protobuf headers for MediaPipe types used
#include "mediapipe/framework/formats/landmark.pb.h"
#include "mediapipe/framework/formats/image_format.pb.h"

// OpenCV
#include <opencv2/opencv.hpp>

////////////////////////////////////////
//           Helper Function          //
////////////////////////////////////////

// returns landmark XYZ data for all detected faces (or empty vector if no detections)
// dimensions = (# faces) x (# landmarks/face) x 3
// i.e., each landmark is a 3-float array (X,Y,Z), so the middle vector contains 468 or 478 of these
// and the outermost vector is for each detected face in the frame
static std::vector<std::vector<std::array<float, 3>>> get_landmarks(const std::shared_ptr<mediapipe::LibMP>& face_mesh) {
	std::vector<std::vector<std::array<float, 3>>> normalized_landmarks;

	// I use a unique_ptr for convenience, so that DeletePacket is called automatically
	// You could also manage deletion yourself, manually:
	// const void* packet = face_mesh->GetOutputPacket("multi_face_landmarks");
	// mediapipe::LibMP::DeletePacket(packet);
	std::unique_ptr<const void, decltype(&mediapipe::LibMP::DeletePacket)> lm_packet_ptr(nullptr, mediapipe::LibMP::DeletePacket);

	// Keep getting packets from queue until empty
	while (face_mesh->GetOutputQueueSize("multi_face_landmarks") > 0) {
		lm_packet_ptr.reset(face_mesh->GetOutputPacket("multi_face_landmarks"));
	}
	if (lm_packet_ptr.get() == nullptr || mediapipe::LibMP::PacketIsEmpty(lm_packet_ptr.get())) {
		return normalized_landmarks; // return empty vector if no output packets or packet is invalid
	}

	// Create multi_face_landmarks from packet's protobuf data
	size_t num_faces = mediapipe::LibMP::GetPacketProtoMsgVecSize(lm_packet_ptr.get());
	for (int face_num = 0; face_num < num_faces; face_num++) {
		// Get reference to protobuf message for face
		const void* lm_list_proto = mediapipe::LibMP::GetPacketProtoMsgAt(lm_packet_ptr.get(), face_num);
		// Get byte size of protobuf message
		size_t lm_list_proto_size = mediapipe::LibMP::GetProtoMsgByteSize(lm_list_proto);

		// Create buffer to hold protobuf message data; copy data to buffer
		std::shared_ptr<uint8_t[]> proto_data(new uint8_t[lm_list_proto_size]);
		mediapipe::LibMP::WriteProtoMsgData(proto_data.get(), lm_list_proto, static_cast<int>(lm_list_proto_size));

		// Initialize a mediapipe::NormalizedLandmarkList object from the buffer
		mediapipe::NormalizedLandmarkList face_landmarks;
		face_landmarks.ParseFromArray(proto_data.get(), static_cast<int>(lm_list_proto_size));

		// Copy the landmark data to our custom data structure
		normalized_landmarks.emplace_back();
		for (const mediapipe::NormalizedLandmark& lm : face_landmarks.landmark()) {
			normalized_landmarks[face_num].push_back({ lm.x(), lm.y(), lm.z() });
		}
	}

	return normalized_landmarks;
}

////////////////////////////////////////
//            Main Function           //
////////////////////////////////////////

int main(int argc, char* argv[]) {
	// adapted from https://github.com/google/mediapipe/blob/master/mediapipe/graphs/face_mesh/face_mesh_desktop_live.pbtxt
	// runs face mesh for up to 1 face with both attention and previous landmark usage enabled
	const char* graph = R"(
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
			output_side_packet: "PACKET:1:use_prev_landmarks"
			output_side_packet: "PACKET:2:with_attention"
			node_options: {
				[type.googleapis.com/mediapipe.ConstantSidePacketCalculatorOptions]: {
					packet { int_value: 1 }
					packet { bool_value: true }
					packet { bool_value: true }
				}
			}
		}
		# Subgraph that detects faces and corresponding landmarks.
		node {
			calculator: "FaceLandmarkFrontCpu"
			input_stream: "IMAGE:throttled_input_video"
			input_side_packet: "NUM_FACES:num_faces"
			input_side_packet: "USE_PREV_LANDMARKS:use_prev_landmarks"
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

	// Create MP face mesh graph
	std::shared_ptr<mediapipe::LibMP> face_mesh(mediapipe::LibMP::Create(graph, "input_video"));

	// MP-rendered output stream of FaceRendererCpu subgraph
	// NOTE: only enable if needed; otherwise, output image packets will queue up & consume memory
	// face_mesh->AddOutputStream("output_video");

	// Landmark XYZ data output stream
	face_mesh->AddOutputStream("multi_face_landmarks");

	// Start MP graph
	face_mesh->Start();

	// Stream from webcam (device #0)
	cv::VideoCapture cap(0);
	if (!cap.isOpened()) {
		std::cerr << "Could not open device #0. Is a camera/webcam attached?" << std::endl;
		return EXIT_FAILURE;
	}

	cv::Mat frame_bgr;
	while (cap.read(frame_bgr)) {
		// Convert frame from BGR to RGB
		cv::Mat frame_rgb;
		cv::cvtColor(frame_bgr, frame_rgb, cv::COLOR_BGR2RGB);

		// Start inference clock
		auto t0 = std::chrono::high_resolution_clock::now();

		// Feed RGB frame into MP face mesh graph (image data is COPIED internally by LibMP)
		if (!face_mesh->Process(frame_rgb.data, frame_rgb.cols, frame_rgb.rows, mediapipe::ImageFormat::SRGB)) {
			std::cerr << "Process() failed!" << std::endl;
			break;
		}
		face_mesh->WaitUntilIdle();

		// Stop inference clock
		auto t1 = std::chrono::high_resolution_clock::now();
		int inference_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();

		// Get landmark coordinates in custom data structure using helper function (see above)
		std::vector<std::vector<std::array<float, 3>>> normalized_landmarks = get_landmarks(face_mesh);

		// For each face, draw a circle at each landmark's position
		size_t num_faces = normalized_landmarks.size();
		for (int face_num = 0; face_num < num_faces; face_num++) {
			for (const std::array<float, 3>& norm_xyz : normalized_landmarks[face_num]) {
				int x = static_cast<int>(norm_xyz[0] * frame_bgr.cols);
				int y = static_cast<int>(norm_xyz[1] * frame_bgr.rows);
				cv::circle(frame_bgr, cv::Point(x, y), 1, cv::Scalar(0, 255, 0), -1);
			}
		}

		// Write some info on frame
		cv::putText(frame_bgr, "Press any key to exit", cv::Point(10, 20), cv::FONT_HERSHEY_PLAIN, 1.0, cv::Scalar(0, 255, 0));
		cv::putText(frame_bgr, "# Faces Detected: " + std::to_string(num_faces), cv::Point(10, 40), cv::FONT_HERSHEY_PLAIN, 1.0, cv::Scalar(0, 255, 0));
		cv::putText(frame_bgr, "Inference time: " + std::to_string(inference_time_ms) + " ms", cv::Point(10, 60), cv::FONT_HERSHEY_PLAIN, 1.0, cv::Scalar(0, 255, 0));

		// Display frame
		cv::imshow("LibMP Example", frame_bgr);

		// Close on any keypress
		if (cv::waitKey(1) >= 0) {
			break;
		}
	}

	cv::destroyAllWindows();
	return EXIT_SUCCESS;
}
