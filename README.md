#### Table of Contents

- [Build and Test Effciency](#build-and-test-effciency)
- [Description](#description)
- [Structure](#structure)
  - [Inputs](#inputs)
    - [Boxes Input](#boxes-input)
    - [Scores Input](#scores-input)
  - [Dynamic Shape Support](#dynamic-shape-support)
  - [Box Coding Type](#box-coding-type)
  - [Outputs](#outputs)
  - [Parameters](#parameters)
- [Algorithm](#algorithm)
  - [Process Description](#process-description)
  - [Performance Tuning](#performance-tuning)
    - [Choosing the Score Threshold](#choosing-the-score-threshold)
    - [Using Sigmoid Activation](#using-sigmoid-activation)
  - [Additional Resources](#additional-resources)
    - [Networks](#networks)
    - [Documentation](#documentation)


## Build and Test Effciency

build
```bash
# change SM in Makefile, refer to https://developer.nvidia.com/cuda-gpus
make
```

test effciency
```bash
python test_plugin.py
```


## Description

This TensorRT plugin implements an efficient algorithm to perform Non Maximum Suppression for rotated object detection networks.

This plugin is modified from  [TensorRT efficientNMSPlugin](https://github.com/NVIDIA/TensorRT/tree/8.4.1/plugin/efficientNMSPlugin) and [MMCV](https://github.com/open-mmlab/mmcv) `nms_rotated` cuda version.

## Structure

### Inputs

The plugin has two modes of operation, depending on the given input data. The plugin will automatically detect which mode to operate as, depending on the number of inputs it receives, as follows:

1. **Standard NMS Mode:** Only two input tensors are given, (i) the bounding box coordinates and (ii) the corresponding classification scores for each box.
2. **Fused Box Decoder Mode:** Three input tensors are given, (i) the raw localization predictions for each box originating directly from the localization head of the network, (ii) the corresponding classification scores originating from the classification head of the network, and (iii) the default anchor box coordinates usually hardcoded as constant tensors in the network.

Most object detection networks work by generating raw predictions from a "localization head" which adjust the coordinates of standard non-learned anchor coordinates to produce a tighter fitting bounding box. This process is called "box decoding", and it usually involves a large number of element-wise operations to transform the anchors to final box coordinates. As this can involve exponential operations on a large number of anchors, it can be computationally expensive, so this plugin gives the option of fusing the box decoder within the NMS operation which can be done in a far more efficient manner, resulting in lower latency for the network.

#### Boxes Input

> **Input Shape:** `[batch_size, number_boxes, 5]` or `[batch_size, number_boxes, number_classes, 5]`
>
> **Data Type:** `float32` or `float16`

The boxes input can have 3 dimensions in case a single box prediction is produced for all classes (such as in EfficientDet or SSD), or 4 dimensions when separate box predictions are generated for each class (such as in FasterRCNN), in which case `number_classes` >= 1 and must match the number of classes in the scores input. The final dimension represents the four coordinates that define the bounding box prediction.

For *Standard NMS* mode, this tensor should contain the final box coordinates for each predicted detection. For *Fused Box Decoder* mode, this tensor should have the raw localization predictions. In either case, this data is given as `4` coordinates which makes up the final shape dimension.

#### Scores Input

> **Input Shape:** `[batch_size, number_boxes, number_classes]`
>
> **Data Type:** `float32` or `float16`

The scores input has `number_classes` elements with the predicted scores for each candidate class for each of the `number_boxes` anchor boxes.

Usually, the score values will have passed through a sigmoid activation function before reaching the NMS operation. However, as an optimization, the pre-sigmoid raw scores can also be provided to the NMS plugin to reduce overall network latency. If raw scores are given, enable the `score_activation` parameter so they are processed accordingly.

### Dynamic Shape Support

Most input shape dimensions, namely `batch_size`, `number_boxes`, and `number_classes`, for all inputs can be defined dynamically at runtime if the TensorRT engine is built with dynamic input shapes. However, once defined, these dimensions must match across all tensors that use them (e.g. the same `number_boxes` dimension must be given for both boxes and scores, etc.)

### Box Coding Type

Different object detection networks represent their box coordinate system differently. The type supported by this plugin is:

1. **BoxCenterSize:** The four coordinates represent `[x, y, w, h, theta]` values, where the x,y pair define the box center location, and the w,h pair define its width and height.

### Outputs

The following four output tensors are generated:

- **num_detections:** This is a `[batch_size, 1]` tensor of data type `int32`. The last dimension is a scalar indicating the number of valid detections per batch image. It can be less than `max_output_boxes`. Only the top `num_detections[i]` entries in `nms_boxes[i]`, `nms_scores[i]` and `nms_classes[i]` are valid.
- **detection_boxes:** This is a `[batch_size, max_output_boxes, 5]` tensor of data type `float32` or `float16`, containing the coordinates of non-max suppressed boxes. The output coordinates will always be in BoxCorner format, regardless of the input code type.
- **detection_scores:** This is a `[batch_size, max_output_boxes]` tensor of data type `float32` or `float16`, containing the scores for the boxes.
- **detection_classes:** This is a `[batch_size, max_output_boxes]` tensor of data type `int32`, containing the classes for the boxes.

### Parameters

| Type    | Parameter            | Description                                                  |
| ------- | -------------------- | ------------------------------------------------------------ |
| `float` | `score_threshold` *  | The scalar threshold for score (low scoring boxes are removed). |
| `float` | `iou_threshold`      | The scalar threshold for IOU (additional boxes that have high IOU overlap with previously selected boxes are removed). |
| `int`   | `max_output_boxes`   | The maximum number of detections to output per image.        |
| `int`   | `background_class`   | The label ID for the background class. If there is no background class, set it to `-1`. |
| `bool`  | `score_activation` * | Set to true to apply sigmoid activation to the confidence scores during NMS operation. |
| `int`   | `box_coding`         | Coding type used for boxes (and anchors if applicable), 0 = BoxCenterSize |

Parameters marked with a `*` have a non-negligible effect on runtime latency. See the [Performance Tuning](#performance-tuning) section below for more details on how to set them optimally.

## Algorithm

### Process Description

The NMS algorithm in this plugin first filters the scores below the given `scoreThreshold`. This subset of scores is then sorted, and their corresponding boxes are then further filtered out by removing boxes that overlap each other with an IOU above the given `iouThreshold`.

The algorithm launcher and its relevant CUDA kernels are all defined in the `efficientNMSInference.cu` file.

Specifically, the NMS algorithm does the following:

- The scores are filtered with the `score_threshold` parameter to reject any scores below the score threshold, while maintaining indexing to cross-reference these scores to their corresponding box coordinates. This is done with the `EfficientNMSFilter` CUDA kernel.
- If too many elements are kept, due to a very low (or zero) score threshold, the filter operation can become a bottleneck due to the atomic operations involved. To mitigate this, a fallback kernel `EfficientNMSDenseIndex` is used instead which passes all the score elements densely packed and indexed. This method is heuristically selected only if the score threshold is less than 0.007.
- The selected scores that remain after filtering are sorted in descending order. The indexing is carefully handled to still maintain score to box relationships after sorting.
- After sorting, the highest 4096 scores are processed by the `EfficientNMS` CUDA kernel. This algorithm uses the index data maintained throughout the previous steps to find the boxes corresponding to the remaining scores. If the fused box decoder is being used, decoding will happen until this stage, where only the top scoring boxes need to be decoded.
- The NMS kernel uses an efficient filtering algorithm that largely reduces the number of IOU overlap cross-checks between box pairs. The boxes that survive the IOU filtering finally pass through to the output results. At this stage, the sigmoid activation is applied to only the final remaining scores, if `score_activation` is enabled, thereby greatly reducing the amount of sigmoid calculations required otherwise.

### Performance Tuning

The plugin implements a very efficient NMS algorithm which largely reduces the latency of this operation in comparison to other NMS plugins. However, there are certain considerations that can help to better fine tune its performance:

#### Choosing the Score Threshold

The algorithm is highly sensitive to the selected `score_threshold` parameter. With a higher threshold, fewer elements need to be processed and so the algorithm runs much faster. Therefore, it's beneficial to always select the highest possible score threshold that fulfills the application requirements. Threshold values lower than approximately 0.01 may cause substantially higher latency.

#### Using Sigmoid Activation

Depending on network configuration, it is usually more efficient to provide raw scores (pre-sigmoid) to the NMS plugin scores input, and enable the `score_activation` parameter. Doing so applies a sigmoid activation only to the last `max_output_boxes` selected scores, instead of all the predicted scores, largely reducing the computational cost.

### Additional Resources

The following resources provide a deeper understanding of the NMS algorithm:

#### Networks

- [EfficientDet](https://arxiv.org/abs/1911.09070)
- [S2ANet](https://github.com/csuhan/s2anet)

#### Documentation

- [NMS algorithm](https://www.coursera.org/lecture/convolutional-neural-networks/non-max-suppression-dvrjH)
- [NonMaxSuppression ONNX Op](https://github.com/onnx/onnx/blob/master/docs/Operators.md#NonMaxSuppression)
- [MMCV](https://github.com/open-mmlab/mmcv) nms_rotated function


