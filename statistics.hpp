

#include <cfloat>
#include <cmath>


#ifndef STATISTICS_HPP_
#define STATISTICS_HPP_


namespace Support
{

	namespace Statistics
	{

		/**
		 * Counter for calculating exponential moving average over the sample
		 * stream.
		 */
		class Average
		{
			public:

				/**
				 * Construct a custom counter with the given N.
				 */
				Average(int N) : m_value(std::nan("1")), m_alpha(2.0 / (N + 1.0)), m_remainder(1.0 - m_alpha) {}

				/**
				 * Return the current value of the counter.
				 */
				double value() const noexcept { return m_value; }

				/**
				 * Update the counter with next sample and return the updated value.
				 */
				double consume(double sample) noexcept
				{
					if (std::isnan(m_value)) {
						m_value = sample;
						return m_value;
					} else {
						m_value = m_alpha * sample + m_remainder * m_value;
						return m_value;
					}
				}

			private:

				double m_value;
				double m_alpha;
				double m_remainder;

		};

		/**
		 * Transformer for ignoring out-of-range values from the sample stream.
		 */
		template<typename T> class Filter
		{
			public:

				/**
				 * Construct a custom transformer that removes sample outside the given range.
				 */
				Filter(T child, double lower, double upper) : m_child(child), m_lower(lower), m_upper(upper) {}

				/**
				 * Return the current value of the underlying counter.
				 */
				double value() const noexcept { return m_child.value(); }

				/**
				 * Update the underlying counter with next sample and return the updated
				 * value. If the given sample is outside the allowed range, the member
				 * function will not submit the sample to the underlying counter, but
				 * only return the current value of the underlying counter.
				 */
				double consume(double sample) noexcept
				{
					if (sample >= m_lower && sample <= m_upper) {
						return m_child.consume(sample);
					} else {
						return m_child.value();
					}
				}

			private:

				T m_child;
				double m_lower;
				double m_upper;

		};

		/**
		 * Transformer for converting samples to their magnitudes. Essentially, it
		 * converts the samples to their absolute values.
		 */
		template<typename T> class Magnitude
		{
			public:

				/**
				 * Construct a custom transformer with the given child.
				 */
				Magnitude(T child) : m_child(child) {}

				/**
				 * Return the current value of the underlying counter.
				 */
				double value() const noexcept { return m_child.value(); }

				/**
				 * Update the underlying counter with next sample and return the updated
				 * value. Note that all samples are converted to their absolute values
				 * before submission.
				 */
				double consume(double sample) noexcept
				{
					if (sample >= 0) {
						return m_child.consume(sample);
					} else {
						return m_child.consume(-sample);
					}
				}

			private:

				T m_child;

		};

		/**
		 * Filter for calculating the divergence of samples from a reference. Note
		 * that the calculated samples will be always positive.
		 */
		template<typename T> class Divergence
		{
			public:

				/**
				 * Construct a custom transformer which converts samples to their
				 * distance from the reference.
				 */
				Divergence(T child, double reference) : m_child(child), m_reference(reference) {}

				/**
				 * Return the current value of the underlying counter.
				 */
				double value() const noexcept { return m_child.value(); };

				/**
				 * Update the underlying counter with next sample and return the updated
				 * value. Note that all samples are converted to their distances from the
				 * reference before submission.
				 */
				double consume(double sample) noexcept
				{
					if (sample >= m_reference) {
						return m_child.consume(sample - m_reference);
					} else {
						return m_child.consume(m_reference - sample);
					}
				}

			private:

				T m_child;
				double m_reference;

		};

		/**
		 * Filter for transforming stream of samples to the delta between samples.
		 * The filtered sample stream will be one element shorter than the input
		 * stream. Also, the derived samples can be positive or negative.
		 */
		template<typename T> class Delta
		{
			public:

				/**
				 * Construct a custom transformer with the given child.
				 */
				Delta(T child) : m_child(child), m_previous(std::nan("1")) {}

				/**
				 * Return the current value of the underlying counter.
				 */
				double value() const noexcept { return m_child.value(); }

				/**
				 * Update the underlying counter with next sample and return the updated
				 * value. The member function will remember the previous sample and then
				 * subtract it from the current sample before submitting it onwards.
				 */
				double consume(double sample) noexcept
				{
					if (std::isnan(m_previous)) {
						m_previous = sample;
						return m_child.value();
					} else {
						double previous = m_previous;
						m_previous = sample;
						return m_child.consume(sample - previous);
					}
				}

			private:

				T m_child;
				double m_previous;

		};

		Average make_average() { return Average(1); }
		Average make_average(int N) { return Average(N); }
		template<typename T> Filter<T> make_filter(T child) { return Filter<T>(child, DBL_MIN, DBL_MAX); }
		template<typename T> Filter<T> make_filter(T child, double lower, double upper) { return Filter<T>(child, lower, upper); }
		template<typename T> Magnitude<T> make_magnitude(T child) { return Magnitude<T>(child); }
		template<typename T> Divergence<T> make_divergence(T child) { return Divergence<T>(child, 0.0); }
		template<typename T> Divergence<T> make_divergence(T child, double reference) { return Divergence<T>(child, reference); }
		template<typename T> Delta<T> make_delta(T child) { return Delta<T>(child); }

	}

};


#endif


