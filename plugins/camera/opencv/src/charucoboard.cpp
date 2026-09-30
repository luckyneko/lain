#include "charucoboard.h"

#include <numeric>
#include <vector>

namespace lain::camera::opencv
{
	cv::aruco::PredefinedDictionaryType toOpenCV(board::Dictionary dictionary)
	{
		using D = board::Dictionary;
		switch (dictionary)
		{
			case D::Aruco4x4_50:
				return cv::aruco::DICT_4X4_50;
			case D::Aruco4x4_100:
				return cv::aruco::DICT_4X4_100;
			case D::Aruco4x4_250:
				return cv::aruco::DICT_4X4_250;
			case D::Aruco4x4_1000:
				return cv::aruco::DICT_4X4_1000;
			case D::Aruco5x5_50:
				return cv::aruco::DICT_5X5_50;
			case D::Aruco5x5_100:
				return cv::aruco::DICT_5X5_100;
			case D::Aruco5x5_250:
				return cv::aruco::DICT_5X5_250;
			case D::Aruco5x5_1000:
				return cv::aruco::DICT_5X5_1000;
			case D::Aruco6x6_50:
				return cv::aruco::DICT_6X6_50;
			case D::Aruco6x6_100:
				return cv::aruco::DICT_6X6_100;
			case D::Aruco6x6_250:
				return cv::aruco::DICT_6X6_250;
			case D::Aruco6x6_1000:
				return cv::aruco::DICT_6X6_1000;
			case D::Aruco7x7_50:
				return cv::aruco::DICT_7X7_50;
			case D::Aruco7x7_100:
				return cv::aruco::DICT_7X7_100;
			case D::Aruco7x7_250:
				return cv::aruco::DICT_7X7_250;
			case D::Aruco7x7_1000:
				return cv::aruco::DICT_7X7_1000;
		}
		return cv::aruco::DICT_4X4_50;
	}

	cv::aruco::CharucoBoard toCharucoBoard(const board::Pattern& pattern, float squareLength)
	{
		const board::PatternParameters& p = pattern.parameters();
		std::vector<int> ids(pattern.markerCount());
		std::iota(ids.begin(), ids.end(), int(p.firstMarkerId));
		cv::aruco::CharucoBoard out(cv::Size(int(p.squaresX), int(p.squaresY)), squareLength,
									float(p.markerToSquare) * squareLength,
									cv::aruco::getPredefinedDictionary(toOpenCV(p.dictionary)), ids);
		out.setLegacyPattern(p.layout == board::CharucoLayout::Legacy);
		return out;
	}
} // namespace lain::camera::opencv
