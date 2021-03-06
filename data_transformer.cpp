#ifdef USE_OPENCV
#include <opencv2/core/core.hpp>
/* Begin Added by garylau, for data augmentation, 2017.11.22 */
#include <opencv2/core/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/opencv.hpp>
/* End Added by garylau, for data augmentation, 2017.11.22 */
#endif  // USE_OPENCV

#include <string>
#include <vector>

#include "caffe/data_transformer.hpp"
#include "caffe/util/io.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/util/rng.hpp"
/* Begin Added by garylau, for data augmentation, 2017.11.22 */
#include <math.h>
#define PI 3.14159265358979323846
/* End Added by garylau, for data augmentation, 2017.11.22 */

namespace caffe {

template<typename Dtype>
DataTransformer<Dtype>::DataTransformer(const TransformationParameter& param,
    Phase phase)
    : param_(param), phase_(phase) {
  // check if we want to use mean_file
  if (param_.has_mean_file()) {
    CHECK_EQ(param_.mean_value_size(), 0) <<
      "Cannot specify mean_file and mean_value at the same time";
    const string& mean_file = param.mean_file();
    if (Caffe::root_solver()) {
      LOG(INFO) << "Loading mean file from: " << mean_file;
    }
    BlobProto blob_proto;
    ReadProtoFromBinaryFileOrDie(mean_file.c_str(), &blob_proto);
    data_mean_.FromProto(blob_proto);
  }
  // check if we want to use mean_value
  if (param_.mean_value_size() > 0) {
    CHECK(param_.has_mean_file() == false) <<
      "Cannot specify mean_file and mean_value at the same time";
    for (int c = 0; c < param_.mean_value_size(); ++c) {
      mean_values_.push_back(param_.mean_value(c));
    }
  }
		/* Begin Added by garylau, for data augmentation, 2017.11.30 */
		// check if we want to use min_side
		if (param_.min_side())
		{
			CHECK_EQ(param_.min_side_min(), 0) << "Cannot specify min_side and min_side_min & min_side_max at the same time";
			CHECK_EQ(param_.min_side_max(), 0) << "Cannot specify min_side and min_side_min & min_side_max at the same time";
		}
		// check if we want to use min_side_min & min_side_max
		if (param_.min_side_min() || param_.min_side_max())
		{
			CHECK_EQ(param_.min_side(), 0) << "Cannot specify min_side_min & min_side_max and min_side at the same time";
			CHECK_GE(param_.min_side_max(), param_.min_side_min()) << "min_side_max must be greater than (or equals to) min_side_min";
		}
		/* End Added by garylau, for data augmentation, 2017.11.30 */
	}

	/* 被读取lmdb图片的Transform调用的Transform, garylau */
template<typename Dtype>
void DataTransformer<Dtype>::Transform(const Datum& datum,
                                       Dtype* transformed_data) {
  const string& data = datum.data();
  const int datum_channels = datum.channels();
  const int datum_height = datum.height();
  const int datum_width = datum.width();

  const int crop_size = param_.crop_size();
  const Dtype scale = param_.scale();
  const bool do_mirror = param_.mirror() && Rand(2);
  const bool has_mean_file = param_.has_mean_file();
  const bool has_uint8 = data.size() > 0;
  const bool has_mean_values = mean_values_.size() > 0;

  CHECK_GT(datum_channels, 0);
  CHECK_GE(datum_height, crop_size);
  CHECK_GE(datum_width, crop_size);

  Dtype* mean = NULL;
  if (has_mean_file) {
    CHECK_EQ(datum_channels, data_mean_.channels());
    CHECK_EQ(datum_height, data_mean_.height());
    CHECK_EQ(datum_width, data_mean_.width());
    mean = data_mean_.mutable_cpu_data();
  }
  if (has_mean_values) {
    CHECK(mean_values_.size() == 1 || mean_values_.size() == datum_channels) <<
     "Specify either 1 mean_value or as many as channels: " << datum_channels;
    if (datum_channels > 1 && mean_values_.size() == 1) {
      // Replicate the mean_value for simplicity
      for (int c = 1; c < datum_channels; ++c) {
        mean_values_.push_back(mean_values_[0]);
      }
    }
  }

  int height = datum_height;
  int width = datum_width;

  int h_off = 0;
  int w_off = 0;
  if (crop_size) {
    height = crop_size;
    width = crop_size;
    // We only do random crop when we do training.
    if (phase_ == TRAIN) {
      h_off = Rand(datum_height - crop_size + 1);
      w_off = Rand(datum_width - crop_size + 1);
    } else {
      h_off = (datum_height - crop_size) / 2;
      w_off = (datum_width - crop_size) / 2;
    }
  }

  Dtype datum_element;
  int top_index, data_index;
  for (int c = 0; c < datum_channels; ++c) {
    for (int h = 0; h < height; ++h) {
      for (int w = 0; w < width; ++w) {
        data_index = (c * datum_height + h_off + h) * datum_width + w_off + w;
        if (do_mirror) {
          top_index = (c * height + h) * width + (width - 1 - w);
        } else {
          top_index = (c * height + h) * width + w;
        }
        if (has_uint8) {
          datum_element =
            static_cast<Dtype>(static_cast<uint8_t>(data[data_index]));
        } else {
          datum_element = datum.float_data(data_index);
        }
        if (has_mean_file) {
          transformed_data[top_index] =
            (datum_element - mean[data_index]) * scale;
        } else {
          if (has_mean_values) {
            transformed_data[top_index] =
              (datum_element - mean_values_[c]) * scale;
          } else {
            transformed_data[top_index] = datum_element * scale;
          }
        }
      }
    }
  }
}

	/* 读取lmdb图片所用到的Transform, garylau */
template<typename Dtype>
void DataTransformer<Dtype>::Transform(const Datum& datum,
                                       Blob<Dtype>* transformed_blob) {
  // If datum is encoded, decoded and transform the cv::image.
  if (datum.encoded()) {
#ifdef USE_OPENCV
    CHECK(!(param_.force_color() && param_.force_gray()))
        << "cannot set both force_color and force_gray";
    cv::Mat cv_img;
    if (param_.force_color() || param_.force_gray()) {
    // If force_color then decode in color otherwise decode in gray.
      cv_img = DecodeDatumToCVMat(datum, param_.force_color());
    } else {
      cv_img = DecodeDatumToCVMatNative(datum);
    }
    // Transform the cv::image into blob.
    return Transform(cv_img, transformed_blob);
#else
    LOG(FATAL) << "Encoded datum requires OpenCV; compile with USE_OPENCV.";
#endif  // USE_OPENCV
  } else {
    if (param_.force_color() || param_.force_gray()) {
      LOG(ERROR) << "force_color and force_gray only for encoded datum";
    }
  }

  const int crop_size = param_.crop_size();
  const int datum_channels = datum.channels();
  const int datum_height = datum.height();
  const int datum_width = datum.width();

  // Check dimensions.
  const int channels = transformed_blob->channels();
  const int height = transformed_blob->height();
  const int width = transformed_blob->width();
  const int num = transformed_blob->num();

  CHECK_EQ(channels, datum_channels);
  CHECK_LE(height, datum_height);
  CHECK_LE(width, datum_width);
  CHECK_GE(num, 1);

  if (crop_size) {
    CHECK_EQ(crop_size, height);
    CHECK_EQ(crop_size, width);
  } else {
    CHECK_EQ(datum_height, height);
    CHECK_EQ(datum_width, width);
  }

  Dtype* transformed_data = transformed_blob->mutable_cpu_data();
  Transform(datum, transformed_data);
}

template<typename Dtype>
void DataTransformer<Dtype>::Transform(const vector<Datum> & datum_vector,
                                       Blob<Dtype>* transformed_blob) {
  const int datum_num = datum_vector.size();
  const int num = transformed_blob->num();
  const int channels = transformed_blob->channels();
  const int height = transformed_blob->height();
  const int width = transformed_blob->width();

  CHECK_GT(datum_num, 0) << "There is no datum to add";
  CHECK_LE(datum_num, num) <<
    "The size of datum_vector must be no greater than transformed_blob->num()";
  Blob<Dtype> uni_blob(1, channels, height, width);
  for (int item_id = 0; item_id < datum_num; ++item_id) {
    int offset = transformed_blob->offset(item_id);
    uni_blob.set_cpu_data(transformed_blob->mutable_cpu_data() + offset);
    Transform(datum_vector[item_id], &uni_blob);
  }
}

#ifdef USE_OPENCV
template<typename Dtype>
void DataTransformer<Dtype>::Transform(const vector<cv::Mat> & mat_vector,
                                       Blob<Dtype>* transformed_blob) {
  const int mat_num = mat_vector.size();
  const int num = transformed_blob->num();
  const int channels = transformed_blob->channels();
  const int height = transformed_blob->height();
  const int width = transformed_blob->width();

  CHECK_GT(mat_num, 0) << "There is no MAT to add";
  CHECK_EQ(mat_num, num) <<
    "The size of mat_vector must be equals to transformed_blob->num()";
  Blob<Dtype> uni_blob(1, channels, height, width);
  for (int item_id = 0; item_id < mat_num; ++item_id) {
    int offset = transformed_blob->offset(item_id);
    uni_blob.set_cpu_data(transformed_blob->mutable_cpu_data() + offset);
    Transform(mat_vector[item_id], &uni_blob);
  }
}

    /* Begin Added by garylau, for data augmentation, 2017.11.22 */
	void rotate(cv::Mat& src, int angle)
	{
		// get rotation matrix for rotating the image around its center
		cv::Point2f center(src.cols / 2.0, src.rows / 2.0);
		cv::Mat rot = cv::getRotationMatrix2D(center, angle, 1.0);
		// determine bounding rectangle
		cv::Rect bbox = cv::RotatedRect(center, src.size(), angle).boundingRect();
		// adjust transformation matrix
		rot.at<double>(0, 2) += bbox.width / 2.0 - center.x;
		rot.at<double>(1, 2) += bbox.height / 2.0 - center.y;
		cv::warpAffine(src, src, rot, bbox.size());
	}

	template <typename Dtype>
	void DataTransformer<Dtype>::random_crop(cv::Mat& cv_img, int crop_size)
	{
		int h_off = 0;
		int w_off = 0;
		const int img_height = cv_img.rows;
		const int img_width = cv_img.cols;

		h_off = Rand(img_height - crop_size + 1);
		w_off = Rand(img_width - crop_size + 1);
		cv::Rect roi(w_off, h_off, crop_size, crop_size);
		cv_img = cv_img(roi);
	}

	void crop_center(cv::Mat& cv_img, int w, int h)
	{
		int h_off = 0;
		int w_off = 0;
		const int img_height = cv_img.rows;
		const int img_width = cv_img.cols;
		h_off = (img_height - h) / 2;
		w_off = (img_width - w) / 2;
		cv::Rect roi(w_off, h_off, w, h);
		cv_img = cv_img(roi);
	}

	void resize(cv::Mat& cv_img, int smallest_side)
	{
		int cur_width = cv_img.cols;
		int cur_height = cv_img.rows;
		cv::Size dsize;
		if (cur_height <= cur_width)
		{
			double k = ((double)cur_height) / smallest_side;
			int new_size = (int)ceil(cur_width / k);
			dsize = cv::Size(new_size, smallest_side);
		}
		else
		{
			double k = ((double)cur_width) / smallest_side;
			int new_size = (int)ceil(cur_height / k);
			dsize = cv::Size(smallest_side, new_size);
		}
		cv::resize(cv_img, cv_img, dsize);
	}
    /* End Added by garylau, for data augmentation, 2017.11.22 */

	/* 读取原始图片所用到的Transform, garylau */
	template<typename Dtype>
	void DataTransformer<Dtype>::Transform(const cv::Mat& img, Blob<Dtype>* transformed_blob)
	{
		const int crop_size = param_.crop_size();
		// Check dimensions.
		const int channels = transformed_blob->channels();
		const int height = transformed_blob->height();
		const int width = transformed_blob->width();
		const int num = transformed_blob->num();
		const Dtype scale = param_.scale();
		const bool has_mean_file = param_.has_mean_file();
		const bool has_mean_values = mean_values_.size() > 0;
		/* Begin Added by garylau, for data augmentation, 2017.11.22 */
		const float apply_prob = 1.f - param_.apply_probability();
		const float max_smooth = param_.max_smooth();
		const int rotation_angle = param_.max_rotation_angle();
		const float min_contrast = param_.min_contrast();
		const float max_contrast = param_.max_contrast();
		const int max_brightness_shift = param_.max_brightness_shift();
		const int max_color_shift = param_.max_color_shift();
		const int min_side_min = param_.min_side_min();
		const int min_side_max = param_.min_side_max();
		const int min_side = param_.min_side();
		const float affine_min_scale = param_.affine_min_scale();
		const float affine_max_scale = param_.affine_max_scale();
		const float random_erasing_low = param_.random_erasing_low();
		const float random_erasing_high = param_.random_erasing_high();
		const float random_erasing_ratio = param_.random_erasing_ratio();
		const bool debug_params = param_.debug_params();

		const bool do_mirror = param_.mirror() && phase_ == TRAIN && Rand(2);
		float current_prob = 0.f;
		caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
		const bool do_smooth = param_.smooth_filtering() && phase_ == TRAIN && max_smooth > 1 && current_prob > apply_prob;
		caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
		const bool do_rotation = rotation_angle > 0 && current_prob > apply_prob && phase_ == TRAIN;
		caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
		const bool do_brightness = param_.contrast_brightness_adjustment() && min_contrast > 0 && max_contrast >= min_contrast
			                       && max_brightness_shift >= 0 && phase_ == TRAIN && current_prob > apply_prob;
		caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
		const bool do_color_shift = max_color_shift > 0 && phase_ == TRAIN && current_prob > apply_prob;
		caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
		const bool do_resize_to_min_side_min_max = min_side_min > 0 && min_side_max > min_side_min && phase_ == TRAIN && current_prob > apply_prob;
		const bool do_resize_to_min_side = min_side > 0 && phase_ == TRAIN && current_prob > apply_prob;
		caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
		const bool do_affine = affine_min_scale > 0 && affine_max_scale > affine_min_scale && phase_ == TRAIN && current_prob > apply_prob;
		caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
		const bool do_random_erasing = param_.random_erasing_ratio() > 0 && param_.random_erasing_high() > param_.random_erasing_low()
			                           && param_.random_erasing_low() > 0 && phase_ == TRAIN && current_prob > apply_prob;

		cv::Mat cv_img = img;
		/* 随机擦除Random-Erasing */
		cv::Scalar erase_mean = cv::mean(cv_img);
		cv::Rect erase_rect;
		if (do_random_erasing)
		{
			int area = cv_img.cols * cv_img.rows;
			caffe_rng_uniform(1, random_erasing_low, random_erasing_high, &current_prob);
			float target_area = current_prob * area;
			caffe_rng_uniform(1, random_erasing_ratio, 1.f / random_erasing_ratio, &current_prob);
			float aspect_ratio = current_prob;
			int erase_height = int(round(sqrt(target_area * aspect_ratio)));   /* 待erase的矩形区域的高 */
			int erase_weight = int(round(sqrt(target_area / aspect_ratio)));   /* 待erase的矩形区域的宽 */
			if (erase_weight <= cv_img.cols && erase_height <= cv_img.rows)
			{
				float erase_x = 0;                                             /* 待erase的矩形区域的左上角x坐标 */
				float erase_y = 0;                                             /* 待erase的矩形区域的左上角y坐标 */
				caffe_rng_uniform(1, 0.f, 1.f * (cv_img.cols - erase_weight), &erase_x);
				caffe_rng_uniform(1, 0.f, 1.f * (cv_img.rows - erase_height), &erase_y);
				erase_rect = cv::Rect(erase_x, erase_y, erase_weight, erase_height);
				if (3 == cv_img.channels())
				{
					cv::Mat_<cv::Vec3b> img_test = cv_img;
					for (size_t i = erase_x; i < erase_x + erase_weight; i++)
					{
						for (size_t j = erase_y; j < erase_y + erase_height; j++)
						{
							img_test(i, j) = cv::Vec3b(erase_mean.val[0], erase_mean.val[1], erase_mean.val[2]);
						}
					}
				}
				else
				{
					cv_img(erase_rect) = erase_mean.val[0];
				}
			}
		}

		// apply color shift
		if (do_color_shift)
		{
			int b = Rand(max_color_shift + 1);
			int g = Rand(max_color_shift + 1);
			int r = Rand(max_color_shift + 1);
			int sign = Rand(2);
			cv::Mat shiftArr = cv_img.clone();
			shiftArr.setTo(cv::Scalar(b, g, r));
			if (sign == 1)
			{
				cv_img -= shiftArr;
			}
			else
			{
				cv_img += shiftArr;
			}
		}

		// set contrast and brightness
		float alpha;
		int beta;
		if (do_brightness)
		{
			caffe_rng_uniform(1, min_contrast, max_contrast, &alpha);
			beta = Rand(max_brightness_shift * 2 + 1) - max_brightness_shift;
			cv_img.convertTo(cv_img, -1, alpha, beta);
		}

		// set smoothness
		int smooth_param = 0;
		int smooth_type = 0;
		if (do_smooth)
		{
			smooth_type = Rand(4);
			smooth_param = 1 + 2 * Rand(max_smooth / 2);
			switch (smooth_type)
			{
			case 0:
				cv::GaussianBlur(cv_img, cv_img, cv::Size(smooth_param, smooth_param), 0);
				break;
			case 1:
				cv::blur(cv_img, cv_img, cv::Size(smooth_param, smooth_param));
				break;
			case 2:
				cv::medianBlur(cv_img, cv_img, smooth_param);
				break;
			case 3:
				cv::boxFilter(cv_img, cv_img, -1, cv::Size(smooth_param * 2, smooth_param * 2));
				break;
			default:
				break;
			}
		}
		/* End Added by garylau, for data augmentation, 2017.11.22 */

		const int img_channels = cv_img.channels();
		const int img_height = cv_img.rows;
		const int img_width = cv_img.cols;

		CHECK_GT(img_channels, 0);
		CHECK_GE(img_height, crop_size);
		CHECK_GE(img_width, crop_size);
		CHECK_EQ(channels, img_channels);
		CHECK_LE(height, img_height);
		CHECK_LE(width, img_width);
		CHECK_GE(num, 1);
		CHECK(cv_img.depth() == CV_8U) << "Image data type must be unsigned byte";

  Dtype* mean = NULL;
  if (has_mean_file) {
    CHECK_EQ(img_channels, data_mean_.channels());
    CHECK_EQ(img_height, data_mean_.height());
    CHECK_EQ(img_width, data_mean_.width());
    mean = data_mean_.mutable_cpu_data();
  }
  if (has_mean_values) {
    CHECK(mean_values_.size() == 1 || mean_values_.size() == img_channels) <<
     "Specify either 1 mean_value or as many as channels: " << img_channels;
    if (img_channels > 1 && mean_values_.size() == 1) {
      // Replicate the mean_value for simplicity
      for (int c = 1; c < img_channels; ++c) {
        mean_values_.push_back(mean_values_[0]);
      }
    }
  }

  /* Begin Added by garylau, for data augmentation, 2017.11.22 */
  // resizing and crop according to min side, preserving aspect ratio
  if (do_resize_to_min_side)
  {
	  random_crop(cv_img, min_side);
  }
  if (do_resize_to_min_side_min_max)
  {
	  int min_side_length = min_side_min + Rand(min_side_max - min_side_min + 1);
	  resize(cv_img, min_side_max);
	  random_crop(cv_img, min_side_length);
  }
  /* 仿射变换 */
  float affine_angle = 0.f;
  float affine_scale = 0.f;
  if (do_affine)
  {
	  cv::Point2f affine_center = cv::Point2f(cv_img.rows / 2, cv_img.cols / 2);
	  affine_angle = 1.0 * Rand(rotation_angle * 2 + 1) - rotation_angle;
	  affine_scale = affine_min_scale + Rand((affine_max_scale - affine_min_scale) * 10) / 10.f;
	  cv::Mat affine_matrix = cv::getRotationMatrix2D(affine_center, affine_angle, affine_scale);
	  cv::Size dize = cv::Size(cv_img.rows * (affine_min_scale + Rand((affine_max_scale - affine_min_scale) * 10) / 10.f),
		  cv_img.cols * (affine_min_scale + Rand((affine_max_scale - affine_min_scale) * 10) / 10.f));
	  cv::warpAffine(cv_img, cv_img, affine_matrix, dize);
  }
  /* 旋转操作 */
  int current_angle = 0;
  if (do_rotation && !do_affine)
  {
	  current_angle = Rand(rotation_angle * 2 + 1) - rotation_angle;
	  if (current_angle)
	  {
		  rotate(cv_img, current_angle);
	  }
  }

  if (debug_params && phase_ == TRAIN) {
	  LOG(INFO) << "----------------------------------------";
	  if (do_smooth)
	  {
		  LOG(INFO) << "* parameter for smooth filtering: ";
		  LOG(INFO) << "  smooth type: " << smooth_type << ", smooth param: " << smooth_param;
	  }
	  if (do_rotation)
	  {
		  LOG(INFO) << "* parameter for rotation: ";
		  LOG(INFO) << "  current rotation angle: " << current_angle;
	  }
	  if (do_brightness)
	  {
		  LOG(INFO) << "* parameter for contrast adjustment: ";
		  LOG(INFO) << "  alpha: " << alpha << ", beta: " << beta;
	  }
	  if (do_color_shift)
	  {
		  LOG(INFO) << "* parameter for color shift: ";
		  LOG(INFO) << "max_color_shift: " << max_color_shift;
	  }
	  if (do_resize_to_min_side_min_max)
	  {
		  LOG(INFO) << "* parameter for min_side_min_max crop: ";
		  LOG(INFO) << "min_side_min: " << min_side_min << ", min_side_max: " << min_side_max;
	  }
	  if (do_resize_to_min_side)
	  {
		  LOG(INFO) << "* parameter for min_side crop: ";
		  LOG(INFO) << "min_side: " << min_side;
	  }
	  if (do_affine)
	  {
		  LOG(INFO) << "* parameter for affine transformation: ";
		  LOG(INFO) << "affine_angle: " << affine_angle << ", affine_scale: " << affine_scale;
	  }
	  if (do_random_erasing)
	  {
		  LOG(INFO) << "* parameter for random erasing: ";
		  LOG(INFO) << "erase_rect: " << "x:" << erase_rect.x << ", y:" << erase_rect.y << ", width:" << erase_rect.width << ", height:" << erase_rect.height;
	  }
  }
  /* End Added by garylau, for data augmentation, 2017.11.22 */

  int h_off = 0;
  int w_off = 0;
  cv::Mat cv_cropped_img = cv_img;
  /* Begin Added by garylau, for data augmentation, 2017.11.22 */
  if (img_width != cv_cropped_img.cols || img_height != cv_cropped_img.rows)
  {
	  cv::resize(cv_cropped_img, cv_cropped_img, cv::Size(img_width, img_height));
  }
  /* End Added by garylau, for data augmentation, 2017.11.22 */
  if (crop_size) {
    CHECK_EQ(crop_size, height);
    CHECK_EQ(crop_size, width);
    // We only do random crop when we do training.
    if (phase_ == TRAIN) {
      h_off = Rand(img_height - crop_size + 1);
      w_off = Rand(img_width - crop_size + 1);
    } else {
      h_off = (img_height - crop_size) / 2;
      w_off = (img_width - crop_size) / 2;
    }
    cv::Rect roi(w_off, h_off, crop_size, crop_size);
	cv_cropped_img = cv_cropped_img(roi);
  } else {
    CHECK_EQ(img_height, height);
    CHECK_EQ(img_width, width);
  }

  CHECK(cv_cropped_img.data);

  Dtype* transformed_data = transformed_blob->mutable_cpu_data();
  int top_index;
  for (int h = 0; h < height; ++h) {
    const uchar* ptr = cv_cropped_img.ptr<uchar>(h);
    int img_index = 0;
    for (int w = 0; w < width; ++w) {
      for (int c = 0; c < img_channels; ++c) {
        if (do_mirror) {
          top_index = (c * height + h) * width + (width - 1 - w);
        } else {
          top_index = (c * height + h) * width + w;
        }
        // int top_index = (c * height + h) * width + w;
        Dtype pixel = static_cast<Dtype>(ptr[img_index++]);
        if (has_mean_file) {
          int mean_index = (c * img_height + h_off + h) * img_width + w_off + w;
          transformed_data[top_index] =
            (pixel - mean[mean_index]) * scale;
        } else {
          if (has_mean_values) {
            transformed_data[top_index] =
              (pixel - mean_values_[c]) * scale;
          } else {
            transformed_data[top_index] = pixel * scale;
          }
        }
      }
    }
  }
}
#endif  // USE_OPENCV

template<typename Dtype>
void DataTransformer<Dtype>::Transform(Blob<Dtype>* input_blob,
                                       Blob<Dtype>* transformed_blob) {
  const int crop_size = param_.crop_size();
  const int input_num = input_blob->num();
  const int input_channels = input_blob->channels();
  const int input_height = input_blob->height();
  const int input_width = input_blob->width();

  if (transformed_blob->count() == 0) {
    // Initialize transformed_blob with the right shape.
    if (crop_size) {
      transformed_blob->Reshape(input_num, input_channels,
                                crop_size, crop_size);
    } else {
      transformed_blob->Reshape(input_num, input_channels,
                                input_height, input_width);
    }
  }

  const int num = transformed_blob->num();
  const int channels = transformed_blob->channels();
  const int height = transformed_blob->height();
  const int width = transformed_blob->width();
  const int size = transformed_blob->count();

  CHECK_LE(input_num, num);
  CHECK_EQ(input_channels, channels);
  CHECK_GE(input_height, height);
  CHECK_GE(input_width, width);


  const Dtype scale = param_.scale();
  const bool do_mirror = param_.mirror() && Rand(2);
  const bool has_mean_file = param_.has_mean_file();
  const bool has_mean_values = mean_values_.size() > 0;

  int h_off = 0;
  int w_off = 0;
  if (crop_size) {
    CHECK_EQ(crop_size, height);
    CHECK_EQ(crop_size, width);
    // We only do random crop when we do training.
    if (phase_ == TRAIN) {
      h_off = Rand(input_height - crop_size + 1);
      w_off = Rand(input_width - crop_size + 1);
    } else {
      h_off = (input_height - crop_size) / 2;
      w_off = (input_width - crop_size) / 2;
    }
  } else {
    CHECK_EQ(input_height, height);
    CHECK_EQ(input_width, width);
  }

  Dtype* input_data = input_blob->mutable_cpu_data();
  if (has_mean_file) {
    CHECK_EQ(input_channels, data_mean_.channels());
    CHECK_EQ(input_height, data_mean_.height());
    CHECK_EQ(input_width, data_mean_.width());
    for (int n = 0; n < input_num; ++n) {
      int offset = input_blob->offset(n);
      caffe_sub(data_mean_.count(), input_data + offset,
            data_mean_.cpu_data(), input_data + offset);
    }
  }

  if (has_mean_values) {
    CHECK(mean_values_.size() == 1 || mean_values_.size() == input_channels) <<
     "Specify either 1 mean_value or as many as channels: " << input_channels;
    if (mean_values_.size() == 1) {
      caffe_add_scalar(input_blob->count(), -(mean_values_[0]), input_data);
    } else {
      for (int n = 0; n < input_num; ++n) {
        for (int c = 0; c < input_channels; ++c) {
          int offset = input_blob->offset(n, c);
          caffe_add_scalar(input_height * input_width, -(mean_values_[c]),
            input_data + offset);
        }
      }
    }
  }

  Dtype* transformed_data = transformed_blob->mutable_cpu_data();

  for (int n = 0; n < input_num; ++n) {
    int top_index_n = n * channels;
    int data_index_n = n * channels;
    for (int c = 0; c < channels; ++c) {
      int top_index_c = (top_index_n + c) * height;
      int data_index_c = (data_index_n + c) * input_height + h_off;
      for (int h = 0; h < height; ++h) {
        int top_index_h = (top_index_c + h) * width;
        int data_index_h = (data_index_c + h) * input_width + w_off;
        if (do_mirror) {
          int top_index_w = top_index_h + width - 1;
          for (int w = 0; w < width; ++w) {
            transformed_data[top_index_w-w] = input_data[data_index_h + w];
          }
        } else {
          for (int w = 0; w < width; ++w) {
            transformed_data[top_index_h + w] = input_data[data_index_h + w];
          }
        }
      }
    }
  }
  if (scale != Dtype(1)) {
    DLOG(INFO) << "Scale: " << scale;
    caffe_scal(size, scale, transformed_data);
  }
}

template<typename Dtype>
vector<int> DataTransformer<Dtype>::InferBlobShape(const Datum& datum) {
  if (datum.encoded()) {
#ifdef USE_OPENCV
    CHECK(!(param_.force_color() && param_.force_gray()))
        << "cannot set both force_color and force_gray";
    cv::Mat cv_img;
    if (param_.force_color() || param_.force_gray()) {
    // If force_color then decode in color otherwise decode in gray.
      cv_img = DecodeDatumToCVMat(datum, param_.force_color());
    } else {
      cv_img = DecodeDatumToCVMatNative(datum);
    }
    // InferBlobShape using the cv::image.
    return InferBlobShape(cv_img);
#else
    LOG(FATAL) << "Encoded datum requires OpenCV; compile with USE_OPENCV.";
#endif  // USE_OPENCV
  }
  const int crop_size = param_.crop_size();
  const int datum_channels = datum.channels();
  const int datum_height = datum.height();
  const int datum_width = datum.width();
  // Check dimensions.
  CHECK_GT(datum_channels, 0);
  CHECK_GE(datum_height, crop_size);
  CHECK_GE(datum_width, crop_size);
  // Build BlobShape.
  vector<int> shape(4);
  shape[0] = 1;
  shape[1] = datum_channels;
  shape[2] = (crop_size)? crop_size: datum_height;
  shape[3] = (crop_size)? crop_size: datum_width;
  return shape;
}

template<typename Dtype>
vector<int> DataTransformer<Dtype>::InferBlobShape(
    const vector<Datum> & datum_vector) {
  const int num = datum_vector.size();
  CHECK_GT(num, 0) << "There is no datum to in the vector";
  // Use first datum in the vector to InferBlobShape.
  vector<int> shape = InferBlobShape(datum_vector[0]);
  // Adjust num to the size of the vector.
  shape[0] = num;
  return shape;
}

#ifdef USE_OPENCV
template<typename Dtype>
vector<int> DataTransformer<Dtype>::InferBlobShape(const cv::Mat& cv_img) {
  const int crop_size = param_.crop_size();
  const int img_channels = cv_img.channels();
  const int img_height = cv_img.rows;
  const int img_width = cv_img.cols;
  // Check dimensions.
  CHECK_GT(img_channels, 0);
  CHECK_GE(img_height, crop_size);
  CHECK_GE(img_width, crop_size);
  // Build BlobShape.
  vector<int> shape(4);
  shape[0] = 1;
  shape[1] = img_channels;
  shape[2] = (crop_size)? crop_size: img_height;
  shape[3] = (crop_size)? crop_size: img_width;
  return shape;
}

template<typename Dtype>
vector<int> DataTransformer<Dtype>::InferBlobShape(
    const vector<cv::Mat> & mat_vector) {
  const int num = mat_vector.size();
  CHECK_GT(num, 0) << "There is no cv_img to in the vector";
  // Use first cv_img in the vector to InferBlobShape.
  vector<int> shape = InferBlobShape(mat_vector[0]);
  // Adjust num to the size of the vector.
  shape[0] = num;
  return shape;
}
#endif  // USE_OPENCV

template <typename Dtype>
void DataTransformer<Dtype>::InitRand() {
  const bool needs_rand = param_.mirror() ||
      (phase_ == TRAIN && param_.crop_size());
  if (needs_rand) {
    const unsigned int rng_seed = caffe_rng_rand();
    rng_.reset(new Caffe::RNG(rng_seed));
  } else {
    rng_.reset();
  }
}

template <typename Dtype>
int DataTransformer<Dtype>::Rand(int n) {
  CHECK(rng_);
  CHECK_GT(n, 0);
  caffe::rng_t* rng =
      static_cast<caffe::rng_t*>(rng_->generator());
  return ((*rng)() % n);
}

INSTANTIATE_CLASS(DataTransformer);

/* Begin Added by garylau, for lmdb data augmentation, 2017.12.11 */
template<typename Dtype>
void DataTransformer<Dtype>::DatumToMat(const Datum* datum, cv::Mat& cv_img)
{
	int datum_channels = datum->channels();
	int datum_height = datum->height();
	int datum_width = datum->width();
	int datum_size = datum_channels * datum_height * datum_width;

	std::string buffer(datum_size, ' ');
	buffer = datum->data();

	for (int h = 0; h < datum_height; ++h) {
		uchar* ptr = cv_img.ptr<uchar>(h);
		int img_index = 0;
		for (int w = 0; w < datum_width; ++w) {
			for (int c = 0; c < datum_channels; ++c) {
				int datum_index = (c * datum_height + h) * datum_width + w;
				ptr[img_index++] = static_cast<uchar>(buffer[datum_index]);
			}
		}
	}
}
template<typename Dtype>
void DataTransformer<Dtype>::MatToDatum(const cv::Mat& cv_img, Datum* datum)
{
	CHECK(cv_img.depth() == CV_8U) << "Image data type must be unsigned byte";
	datum->set_channels(cv_img.channels());
	datum->set_height(cv_img.rows);
	datum->set_width(cv_img.cols);
	datum->clear_data();
	datum->set_encoded(false);
	int datum_channels = datum->channels();
	int datum_height = datum->height();
	int datum_width = datum->width();
	int datum_size = datum_channels * datum_height * datum_width;
	std::string buffer(datum_size, ' ');
	for (int h = 0; h < datum_height; ++h) {
		const uchar* ptr = cv_img.ptr<uchar>(h);
		int img_index = 0;
		for (int w = 0; w < datum_width; ++w) {
			for (int c = 0; c < datum_channels; ++c) {
				int datum_index = (c * datum_height + h) * datum_width + w;
				buffer[datum_index] = static_cast<char>(ptr[img_index++]);
			}
		}
	}
	datum->set_data(buffer);
}
template<typename Dtype>
void DataTransformer<Dtype>::CVMatTransform(cv::Mat& in_out_cv_img)
{
	const float apply_prob = 1.f - param_.apply_probability();
	const float max_smooth = param_.max_smooth();
	const int rotation_angle = param_.max_rotation_angle();
	const float min_contrast = param_.min_contrast();
	const float max_contrast = param_.max_contrast();
	const int max_brightness_shift = param_.max_brightness_shift();
	const int max_color_shift = param_.max_color_shift();
	const int min_side_min = param_.min_side_min();
	const int min_side_max = param_.min_side_max();
	const int min_side = param_.min_side();
	const float affine_min_scale = param_.affine_min_scale();
	const float affine_max_scale = param_.affine_max_scale();
	const float random_erasing_low = param_.random_erasing_low();
	const float random_erasing_high = param_.random_erasing_high();
	const float random_erasing_ratio = param_.random_erasing_ratio();
	const bool debug_params = param_.debug_params();

	float current_prob = 0.f;
	caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
	const bool do_smooth = param_.smooth_filtering() && phase_ == TRAIN && max_smooth > 1 && current_prob > apply_prob;
	caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
	const bool do_rotation = rotation_angle > 0 && current_prob > apply_prob && phase_ == TRAIN;
	caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
	const bool do_brightness = param_.contrast_brightness_adjustment() && min_contrast > 0 && max_contrast >= min_contrast
		&& max_brightness_shift >= 0 && phase_ == TRAIN && current_prob > apply_prob;
	caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
	const bool do_color_shift = max_color_shift > 0 && phase_ == TRAIN && current_prob > apply_prob;
	caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
	const bool do_resize_to_min_side_min_max = min_side_min > 0 && min_side_max > min_side_min && phase_ == TRAIN && current_prob > apply_prob;
	const bool do_resize_to_min_side = min_side > 0 && phase_ == TRAIN && current_prob > apply_prob;
	caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
	const bool do_affine = affine_min_scale > 0 && affine_max_scale > affine_min_scale && phase_ == TRAIN && current_prob > apply_prob;
	caffe_rng_uniform(1, 0.f, 1.f, &current_prob);
	const bool do_random_erasing = param_.random_erasing_ratio() > 0 && param_.random_erasing_high() > param_.random_erasing_low()
		&& param_.random_erasing_low() > 0 && phase_ == TRAIN && current_prob > apply_prob;

	cv::Mat cv_img = in_out_cv_img;
	/* 随机擦除Random-Erasing */
	cv::Scalar erase_mean = cv::mean(cv_img);
	cv::Rect erase_rect;
	if (do_random_erasing)
	{
		int area = cv_img.cols * cv_img.rows;
		caffe_rng_uniform(1, random_erasing_low, random_erasing_high, &current_prob);
		float target_area = current_prob * area;
		caffe_rng_uniform(1, random_erasing_ratio, 1.f / random_erasing_ratio, &current_prob);
		float aspect_ratio = current_prob;
		int erase_height = int(round(sqrt(target_area * aspect_ratio)));   /* 待erase的矩形区域的高 */
		int erase_weight = int(round(sqrt(target_area / aspect_ratio)));   /* 待erase的矩形区域的宽 */
		if (erase_weight <= cv_img.cols && erase_height <= cv_img.rows)
		{
			float erase_x = 0;                                             /* 待erase的矩形区域的左上角x坐标 */
			float erase_y = 0;                                             /* 待erase的矩形区域的左上角y坐标 */
			caffe_rng_uniform(1, 0.f, 1.f * (cv_img.cols - erase_weight), &erase_x);
			caffe_rng_uniform(1, 0.f, 1.f * (cv_img.rows - erase_height), &erase_y);
			erase_rect = cv::Rect(erase_x, erase_y, erase_weight, erase_height);
			if (3 == cv_img.channels())
			{
				cv::Mat_<cv::Vec3b> img_test = cv_img;
				for (size_t i = erase_x; i < erase_x + erase_weight; i++)
				{
					for (size_t j = erase_y; j < erase_y + erase_height; j++)
					{
						img_test(i, j) = cv::Vec3b(erase_mean.val[0], erase_mean.val[1], erase_mean.val[2]);
					}
				}
			}
			else
			{
				cv_img(erase_rect) = erase_mean.val[0];
			}
		}
	}

	// apply color shift
	if (do_color_shift)
	{
		int b = Rand(max_color_shift + 1);
		int g = Rand(max_color_shift + 1);
		int r = Rand(max_color_shift + 1);
		int sign = Rand(2);
		cv::Mat shiftArr = cv_img.clone();
		shiftArr.setTo(cv::Scalar(b, g, r));
		if (sign == 1)
		{
			cv_img -= shiftArr;
		}
		else
		{
			cv_img += shiftArr;
		}
	}

	// set contrast and brightness
	float alpha;
	int beta;
	if (do_brightness)
	{
		caffe_rng_uniform(1, min_contrast, max_contrast, &alpha);
		beta = Rand(max_brightness_shift * 2 + 1) - max_brightness_shift;
		cv_img.convertTo(cv_img, -1, alpha, beta);
	}

	// set smoothness
	int smooth_param = 0;
	int smooth_type = 0;
	if (do_smooth)
	{
		smooth_type = Rand(4);
		smooth_param = 1 + 2 * Rand(max_smooth / 2);
		switch (smooth_type)
		{
		case 0:
			cv::GaussianBlur(cv_img, cv_img, cv::Size(smooth_param, smooth_param), 0);
			break;
		case 1:
			cv::blur(cv_img, cv_img, cv::Size(smooth_param, smooth_param));
			break;
		case 2:
			cv::medianBlur(cv_img, cv_img, smooth_param);
			break;
		case 3:
			cv::boxFilter(cv_img, cv_img, -1, cv::Size(smooth_param * 2, smooth_param * 2));
			break;
		default:
			break;
		}
	}

	const int img_height = cv_img.rows;
	const int img_width = cv_img.cols;

	CHECK(cv_img.depth() == CV_8U) << "Image data type must be unsigned byte";

	// resizing and crop according to min side, preserving aspect ratio
	if (do_resize_to_min_side)
	{
		random_crop(cv_img, min_side);
	}
	if (do_resize_to_min_side_min_max)
	{
		int min_side_length = min_side_min + Rand(min_side_max - min_side_min + 1);
		resize(cv_img, min_side_max);
		random_crop(cv_img, min_side_length);
	}
	/* 仿射变换 */
	float affine_angle = 0.f;
	float affine_scale = 0.f;
	if (do_affine)
	{
		cv::Point2f affine_center = cv::Point2f(cv_img.rows / 2, cv_img.cols / 2);
		affine_angle = 1.0 * Rand(rotation_angle * 2 + 1) - rotation_angle;
		affine_scale = affine_min_scale + Rand((affine_max_scale - affine_min_scale) * 10) / 10.f;
		cv::Mat affine_matrix = cv::getRotationMatrix2D(affine_center, affine_angle, affine_scale);
		cv::Size dize = cv::Size(cv_img.rows * (affine_min_scale + Rand((affine_max_scale - affine_min_scale) * 10) / 10.f),
			cv_img.cols * (affine_min_scale + Rand((affine_max_scale - affine_min_scale) * 10) / 10.f));
		cv::warpAffine(cv_img, cv_img, affine_matrix, dize);
	}
	/* 旋转操作 */
	int current_angle = 0;
	if (do_rotation && !do_affine)
	{
		current_angle = Rand(rotation_angle * 2 + 1) - rotation_angle;
		if (current_angle)
		{
			rotate(cv_img, current_angle);
		}
	}

	if (debug_params && phase_ == TRAIN) {
		LOG(INFO) << "----------------------------------------";
		if (do_smooth)
		{
			LOG(INFO) << "* parameter for smooth filtering: ";
			LOG(INFO) << "  smooth type: " << smooth_type << ", smooth param: " << smooth_param;
		}
		if (do_rotation)
		{
			LOG(INFO) << "* parameter for rotation: ";
			LOG(INFO) << "  current rotation angle: " << current_angle;
		}
		if (do_brightness)
		{
			LOG(INFO) << "* parameter for contrast adjustment: ";
			LOG(INFO) << "  alpha: " << alpha << ", beta: " << beta;
		}
		if (do_color_shift)
		{
			LOG(INFO) << "* parameter for color shift: ";
			LOG(INFO) << "max_color_shift: " << max_color_shift;
		}
		if (do_resize_to_min_side_min_max)
		{
			LOG(INFO) << "* parameter for min_side_min_max crop: ";
			LOG(INFO) << "min_side_min: " << min_side_min << ", min_side_max: " << min_side_max;
		}
		if (do_resize_to_min_side)
		{
			LOG(INFO) << "* parameter for min_side crop: ";
			LOG(INFO) << "min_side: " << min_side;
		}
		if (do_affine)
		{
			LOG(INFO) << "* parameter for affine transformation: ";
			LOG(INFO) << "affine_angle: " << affine_angle << ", affine_scale: " << affine_scale;
		}
		if (do_random_erasing)
		{
			LOG(INFO) << "* parameter for random erasing: ";
			LOG(INFO) << "erase_rect: " << "x:" << erase_rect.x << ", y:" << erase_rect.y << ", width:" << erase_rect.width << ", height:" << erase_rect.height;
		}
	}

	int h_off = 0;
	int w_off = 0;
	if (img_width != cv_img.cols || img_height != cv_img.rows)
	{
		cv::resize(cv_img, cv_img, cv::Size(img_width, img_height));
	}
	in_out_cv_img = cv_img;
}
/* End Added by garylau, for lmdb data augmentation, 2017.12.11 */

}  // namespace caffe
