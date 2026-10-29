#include "opencvmatcher.h"

#include <opencv2/core/utility.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/flann.hpp>

#include <mutex>

namespace lain::camera::opencv
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Building a FLANN index draws from the process-global std::rand (opencv2/flann/random.h), and
	// lain matches camera pairs in parallel, so two builds at once would race on its state. Only the
	// build draws: a search of a built index does not, so searches stay parallel.
	static std::mutex& flannBuildMutex()
	{
		static std::mutex instance;
		return instance;
	}

	// The descriptors as floats, one row each. FLANN's KD-trees need floats, and the brute-force
	// search is 3.9 times faster over them than over the bytes (measured: 0.61 s against 2.37 s for
	// 3891 x 3688 SIFT descriptors, both directions) with identical answers, since every partial sum
	// of squared byte differences is an integer below 2^24, which a float holds exactly.
	static cv::Mat rows(const feature::Features& f)
	{
		const cv::Mat bytes(int(f.size()), int(f.descriptorBytes), CV_8U, const_cast<std::uint8_t*>(f.descriptors.data()));
		cv::Mat out;
		bytes.convertTo(out, CV_32F);
		return out;
	}

	// --- OpenCVMatcher ------------------------------------------------------------

	Provenance OpenCVMatcher::provenance() const
	{
		return {"opencv", cv::getVersionString()};
	}

	bool OpenCVMatcher::accepts(std::string_view kind) const
	{
		return kind == "sift";
	}

	bool OpenCVMatcher::supports(feature::MatchSearch) const
	{
		return true;
	}

	std::vector<std::array<feature::Neighbour, 2>> OpenCVMatcher::nearest(const feature::Features& a, const feature::Features& b,
																		  feature::MatchSearch search) const
	{
		std::vector<std::array<feature::Neighbour, 2>> out;
		try
		{
			std::vector<std::vector<cv::DMatch>> found;
			const cv::Mat queries = rows(a), train = rows(b);
			if (search == feature::MatchSearch::Exact)
				cv::BFMatcher(cv::NORM_L2).knnMatch(queries, train, found, 2);
			else
			{
				cv::FlannBasedMatcher matcher(cv::makePtr<cv::flann::KDTreeIndexParams>(4),
											  cv::makePtr<cv::flann::SearchParams>(32));
				matcher.add(std::vector<cv::Mat>{train});
				{
					const std::lock_guard<std::mutex> lock(flannBuildMutex());
					matcher.train();
				}
				matcher.knnMatch(queries, found, 2);
			}
			out.reserve(found.size());
			for (const std::vector<cv::DMatch>& pair : found)
			{
				if (pair.size() < 2)
					return {}; // malformed, and the facade says so
				out.push_back({feature::Neighbour{std::uint32_t(pair[0].trainIdx), double(pair[0].distance)},
							   feature::Neighbour{std::uint32_t(pair[1].trainIdx), double(pair[1].distance)}});
			}
		}
		catch (const cv::Exception&)
		{
			return {};
		}
		return out;
	}
} // namespace lain::camera::opencv
