#include "lain/camera/serialize/board.h"

#include <utility>

namespace lain::camera::board
{
	void serialize(data::Archive& archive, Instance& instance)
	{
		const MeasuredLength& square = instance.squareLength;
		double millimetres = square.value.as<core::Length::Millimetres>();
		std::optional<double> lower;
		std::optional<double> upper;
		if (square.lowerBound)
			lower = square.lowerBound->as<core::Length::Millimetres>();
		if (square.upperBound)
			upper = square.upperBound->as<core::Length::Millimetres>();

		archive.member("identity", instance.identity)
			.member("squareLengthMm", millimetres)
			.member("squareLowerBoundMm", lower)
			.member("squareUpperBoundMm", upper);

		if (archive.loading())
		{
			instance.squareLength.value = core::Length::from<core::Length::Millimetres>(millimetres);
			instance.squareLength.lowerBound =
				lower ? std::optional<core::Length>(core::Length::from<core::Length::Millimetres>(*lower)) : std::nullopt;
			instance.squareLength.upperBound =
				upper ? std::optional<core::Length>(core::Length::from<core::Length::Millimetres>(*upper)) : std::nullopt;
		}
	}

	data::Value specificationToValue(const Specification& specification)
	{
		return data::toValue(SpecificationParameters{specification.pattern().parameters(), specification.instance()});
	}

	SpecificationRead specificationFromValue(const data::Value& document)
	{
		data::StrictRead<SpecificationParameters> strict = data::fromValueStrict<SpecificationParameters>(document);
		if (!strict.value)
		{
			SpecificationRead result;
			result.problems = std::move(strict.problems);
			return result;
		}
		return specificationFrom(*strict.value);
	}

	SpecificationRead specificationFrom(const SpecificationParameters& parameters)
	{
		SpecificationRead result;
		const PatternResult pattern = Pattern::create(parameters.pattern);
		for (const PatternDiagnostic& diagnostic : pattern.diagnostics)
			result.problems.push_back(diagnostic.detail);
		if (!pattern.pattern)
			return result;
		SpecificationResult specification = Specification::create(*pattern.pattern, parameters.instance);
		for (const SpecificationDiagnostic& diagnostic : specification.diagnostics)
			result.problems.push_back(diagnostic.detail);
		result.specification = std::move(specification.specification);
		return result;
	}
} // namespace lain::camera::board
