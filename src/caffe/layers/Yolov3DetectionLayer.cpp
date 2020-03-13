#include <algorithm>
#include <fstream>  // NOLINT(readability/streams)
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "boost/filesystem.hpp"
#include "boost/foreach.hpp"

#include "caffe/layers/Yolov3DetectionLayer.hpp"
#include "caffe/util/io.hpp"
#include "caffe/util/bbox_util.hpp"

namespace caffe {
template <typename Dtype>
inline Dtype sigmoid(Dtype x){
	return 1. / (1. + exp(-x));
}
template <typename Dtype>
class PredictionResult {
public:
	Dtype x;
	Dtype y;
	Dtype w;
	Dtype h;
	Dtype objScore;
	Dtype classScore;
	Dtype confidence;
	int classType;
};
template <typename Dtype>
Dtype overlap(Dtype x1, Dtype w1, Dtype x2, Dtype w2)
{
	float l1 = x1 - w1 / 2;
	float l2 = x2 - w2 / 2;
	float left = l1 > l2 ? l1 : l2;
	float r1 = x1 + w1 / 2;
	float r2 = x2 + w2 / 2;
	float right = r1 < r2 ? r1 : r2;
	return right - left;
}
template <typename Dtype>
Dtype box_intersection(vector<Dtype> a, vector<Dtype> b)
{
	float w = overlap(a[0], a[2], b[0], b[2]);
	float h = overlap(a[1], a[3], b[1], b[3]);
	if (w < 0 || h < 0) return 0;
	float area = w*h;
	return area;
}
template <typename Dtype>
Dtype box_union(vector<Dtype> a, vector<Dtype> b)
{
	float i = box_intersection(a, b);
	float u = a[2] * a[3] + b[2] * b[3] - i;
	return u;
}
template <typename Dtype>
Dtype box_iou(vector<Dtype> a, vector<Dtype> b)
{
	return box_intersection(a, b) / box_union(a, b);
}
template <typename Dtype>
void setNormalizedBBox(NormalizedBBox& bbox, Dtype x, Dtype y, Dtype w, Dtype h){
	Dtype xmin = x - w / 2.0;
	Dtype xmax = x + w / 2.0;
	Dtype ymin = y - h / 2.0;
	Dtype ymax = y + h / 2.0;

	if (xmin < 0.0) {
		xmin = 0.0;
	}
	if (xmax > 1.0) {
		xmax = 1.0;
	}
	if (ymin < 0.0) {
		ymin = 0.0;
	}
	if (ymax > 1.0) {
		ymax = 1.0;
	}
	bbox.set_xmin(xmin);
	bbox.set_ymin(ymin);
	bbox.set_xmax(xmax);
	bbox.set_ymax(ymax);
	float bbox_size = BBoxSize(bbox, true);
	bbox.set_size(bbox_size);
}
template <typename Dtype>
void ApplyNms(vector< PredictionResult<Dtype> >& boxes, vector<int>& idxes, Dtype threshold) {
	map<int, int> idx_map;
	for (int i = 0; i < boxes.size() - 1; ++i) {
		if (idx_map.find(i) != idx_map.end()) {
			continue;
		}
		for (int j = i + 1; j < boxes.size(); ++j) {
			if (idx_map.find(j) != idx_map.end()) {
				continue;
			}
			vector<Dtype> Bbox1, Bbox2;
			Bbox1.push_back(boxes[i].x);
			Bbox1.push_back(boxes[i].y);
			Bbox1.push_back(boxes[i].w);
			Bbox1.push_back(boxes[i].h);

			Bbox2.push_back(boxes[j].x);
			Bbox2.push_back(boxes[j].y);
			Bbox2.push_back(boxes[j].w);
			Bbox2.push_back(boxes[j].h);

			Dtype iou = box_iou(Bbox1, Bbox2);
			if (iou >= threshold) {
				idx_map[j] = 1;
			}
		}
	}
	for (int i = 0; i < boxes.size(); ++i) {
		if (idx_map.find(i) == idx_map.end()) {
			idxes.push_back(i);
		}
	}
}
template <typename Dtype>
void class_index_and_score(Dtype* input, int classes, PredictionResult<Dtype>& predict)
{
	Dtype sum = 0;
	Dtype large = input[0];
	int classIndex = 0;
	for (int i = 0; i < classes; ++i) {
		if (input[i] > large)
			large = input[i];
	}
	for (int i = 0; i < classes; ++i) {
		Dtype e = exp(input[i] - large);
		sum += e;
		input[i] = e;
	}

	for (int i = 0; i < classes; ++i) {
		input[i] = input[i] / sum;
	}
	large = input[0];
	classIndex = 0;

	for (int i = 0; i < classes; ++i) {
		if (input[i] > large) {
			large = input[i];
			classIndex = i;
		}
	}
	predict.classType = classIndex;
	predict.classScore = large;
}
template <typename Dtype>
void get_yolo_box(vector<Dtype> &b, Dtype* x, vector<Dtype> biases, int n, 
						int index, int i, int j, int lw, int lh, int w, int h, int stride) {
	b.clear();
	b.push_back((i + (x[index + 0 * stride])) / lw);
	b.push_back((j + (x[index + 1 * stride])) / lh);
	b.push_back(exp(x[index + 2 * stride]) * biases[2 * n] / (w));
	b.push_back(exp(x[index + 3 * stride]) * biases[2 * n + 1] / (h));
}
template <typename Dtype>
void Yolov3DetectionLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  const Yolov3DetectionOutputParameter& yolov3_detection_output_param =
      this->layer_param_.yolov3_detection_output_param();
  CHECK(yolov3_detection_output_param.has_num_classes()) << "Must specify num_classes";
  side_ = bottom[0]->width();
  num_class_ = yolov3_detection_output_param.num_classes();
  mask_num_box_ = yolov3_detection_output_param.num_box();
  coords_ = 4;
  confidence_threshold_ = yolov3_detection_output_param.confidence_threshold();
  nms_threshold_ = yolov3_detection_output_param.nms_threshold();
  mask_num_group_ = yolov3_detection_output_param.mask_group_num();
  for (int c = 0; c < yolov3_detection_output_param.biases_size(); ++c) {
     biases_.push_back(yolov3_detection_output_param.biases(c));
  } 
  for (int c = 0; c < yolov3_detection_output_param.mask_size(); ++c) {
	  mask_.push_back(yolov3_detection_output_param.mask(c));
  } 
  for (int c = 0; c < yolov3_detection_output_param.anchors_scale_size(); ++c) {
	  anchors_scale_.push_back(yolov3_detection_output_param.anchors_scale(c));
  }
  groups_num_ = yolov3_detection_output_param.mask_size() / mask_num_group_;
  
  CHECK_EQ(bottom.size(), mask_num_group_);
}

template <typename Dtype>
void Yolov3DetectionLayer<Dtype>::Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  vector<int> top_shape(2, 1);
  // Since the number of bboxes to be kept is unknown before nms, we manually
  // set it to (fake) 1.
  top_shape.push_back(1);
  // Each row is a 7 dimension vector, which stores
  // [image_id, label, confidence, x, y, w, h]
  top_shape.push_back(7);
  top[0]->Reshape(top_shape);
}
template <typename Dtype>
bool BoxSortDecendScore(const PredictionResult<Dtype>& box1, const PredictionResult<Dtype>& box2) {
	return box1.confidence > box2.confidence;
}
template <typename Dtype>
void Yolov3DetectionLayer<Dtype>::Forward_cpu(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
	const int num = bottom[0]->num();
	
	int len = 4 + 1 + num_class_;

	int mask_offset = 0;
	vector< PredictionResult<Dtype> > predicts;
	predicts.clear();

	std::vector<float> class_score(num_class_, 0.f);
	
	for (int t = 0; t < bottom.size(); t++) {
		side_ = bottom[t]->width();
		int stride = side_*side_;
		swap_.ReshapeLike(*bottom[t]);
		Dtype* swap_data = swap_.mutable_cpu_data();
		const Dtype* input_data = bottom[t]->cpu_data();
		const int layerDim = len * mask_num_box_* side_ * side_ ;
		for (int b = 0; b < bottom[t]->num(); b++) {
			for (int s = 0; s < side_*side_; s++) {
				for (int n = 0; n < mask_num_box_; n++) {
					int channelIndex = b*layerDim + n*len*stride + s;
					vector<Dtype> pred;
					for (int c = 0; c < len; ++c) {
						int index2 = c*stride + channelIndex;
						if (c == 2 || c == 3) {
							swap_data[index2] = (input_data[index2]);
						}else {
							if (c > 4) {
								class_score[c - 5] = sigmoid(input_data[index2]);
							}else {
								swap_data[index2] = sigmoid(input_data[index2]);
							}
						}
					}
					int point_y = s / side_;
					int point_x = s % side_;
					Dtype obj_score = swap_data[channelIndex + 4 * stride];
					get_yolo_box(pred, swap_data, biases_, mask_[n + mask_offset], 
									channelIndex, point_x, point_y, side_, side_, side_*anchors_scale_[t], 
																	side_*anchors_scale_[t], stride);
					PredictionResult<Dtype> predict;
					for (int c = 0; c < num_class_; ++c) {
						class_score[c] *= obj_score;
						if (class_score[c] > confidence_threshold_){						
							predict.x = pred[0];
							predict.y = pred[1];
							predict.w = pred[2];
							predict.h = pred[3];
							predict.classType = c ;
							predict.confidence = class_score[c];
							predicts.push_back(predict);
						}
					}
				}
			}
		}
		mask_offset += groups_num_;
		
	}

	std::sort(predicts.begin(), predicts.end(), BoxSortDecendScore<Dtype>);
    vector<int> idxes;
    int num_kept = 0;
    if(predicts.size() > 0){
		ApplyNms(predicts, idxes, nms_threshold_);
		num_kept = idxes.size();
    }
    vector<int> top_shape(2, 1);
    top_shape.push_back(num_kept);
    top_shape.push_back(7);

    Dtype* top_data;
  
  if (num_kept == 0) {
    DLOG(INFO) << "Couldn't find any detections";
    top_shape[2] = swap_.num();
    top[0]->Reshape(top_shape);
    top_data = top[0]->mutable_cpu_data();
    caffe_set<Dtype>(top[0]->count(), -1, top_data);
    // Generate fake results per image.
    for (int i = 0; i < num; ++i) {
      top_data[0] = i;
      top_data += 7;
    }
  }else {
    top[0]->Reshape(top_shape);
    top_data = top[0]->mutable_cpu_data();
    for (int i = 0; i < num_kept; i++){
      top_data[i*7] = 0;                              //Image_Id
      top_data[i*7+1] = predicts[idxes[i]].classType + 1; //label, 为了迎合假设有背景所以 + 1
      top_data[i*7+2] = predicts[idxes[i]].confidence; //confidence， 类别置信度

	  float left = (predicts[idxes[i]].x - predicts[idxes[i]].w / 2.);
	  float right = (predicts[idxes[i]].x + predicts[idxes[i]].w / 2.);
	  float top = (predicts[idxes[i]].y - predicts[idxes[i]].h / 2.);
	  float bot = (predicts[idxes[i]].y + predicts[idxes[i]].h / 2.);

      top_data[i*7 + 3] = left;
      top_data[i*7 + 4] = top;
      top_data[i*7 + 5] = right;
      top_data[i*7 + 6] = bot;
	  DLOG(INFO) << "Detection box"  << ",classType: " << predicts[idxes[i]].classType 
	  				<< ", x: " << predicts[idxes[i]].x << ", y: " << predicts[idxes[i]].y 
					<< ", w: " << predicts[idxes[i]].w << ", h: " << predicts[idxes[i]].h;
    }

  }

}

#ifdef CPU_ONLY
STUB_GPU_FORWARD(Yolov3DetectionLayer, Forward);
#endif

INSTANTIATE_CLASS(Yolov3DetectionLayer);
REGISTER_LAYER_CLASS(Yolov3Detection);

}  // namespace caffe