#include "lain/camera/serialize/board.h"

#include "strict.h"

#include <utility>

namespace lain::camera::board
{
	void serialize(data::Archive& archive, Instance& instance)
	{
		const MeasuredLength& square = instance.squareLength;
		double millimetres = square.value.millimetres();
		std::optional<double> lower;
		std::optional<double> upper;
		if (square.lowerBound)
			lower = square.lowerBound->millimetres();
		if (square.upperBound)
			upper = square.upperBound->millimetres();

		archive.member("identity", instance.identity)
			.member("squareLengthMm", millimetres)
			.member("squareLowerBoundMm", lower)
			.member("squareUpperBoundMm", upper);

		if (archive.loading())
		{
			instance.squareLength.value = core::Length::fromMillimetres(millimetres);
			instance.squareLength.lowerBound =
				lower ? std::optional<core::Length>(core::Length::fromMillimetres(*lower)) : std::nullopt;
			instance.squareLength.upperBound =
				upper ? std::optional<core::Length>(core::Length::fromMillimetres(*upper)) : std::nullopt;
		}
	}

	// The document's shape, as a struct the reflection can walk: a Specification itself is built only
	// through create().
	struct SpecificationDocument
	{
		PatternParameters pattern;
		Instance instance;
	};
	LAIN_SERIALIZE(SpecificationDocument, pattern, instance)

	data::Value specificationToValue(const Specification& specification)
	{
		return data::toValue(SpecificationDocument{specification.pattern().parameters(), specification.instance()});
	}

	SpecificationRead specificationFromValue(const data::Value& document)
	{
		SpecificationRead result;
		const std::optional<SpecificationDocument> read = data::fromValue<SpecificationDocument>(document);
		if (!read)
		{
			result.problems.push_back("the document is not a board specification");
			return result;
		}
		result.problems = detail::shapeDifferences(document, data::toValue(*read));
		if (!result.problems.empty())
			return result;

		const PatternResult pattern = Pattern::create(read->pattern);
		for (const PatternDiagnostic& diagnostic : pattern.diagnostics)
			result.problems.push_back(diagnostic.detail);
		if (!pattern.pattern)
			return result;
		SpecificationResult specification = Specification::create(*pattern.pattern, read->instance);
		for (const SpecificationDiagnostic& diagnostic : specification.diagnostics)
			result.problems.push_back(diagnostic.detail);
		result.specification = std::move(specification.specification);
		return result;
	}
} // namespace lain::camera::board
