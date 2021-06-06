#ifndef INTERP__BSPLINE_HPP_
#define INTERP__BSPLINE_HPP_

#include <iostream>
#include <ranges>

#include "smooth/concepts.hpp"
#include "smooth/meta.hpp"

namespace smooth {

namespace detail {

/**
 * @brief Calculate cardinal bspline coefficient matrix at compile-time
 * @tparam Scalar
 * @tparam K degree of bspline
 */
template<typename Scalar, std::size_t K>
constexpr ::smooth::meta::StaticMatrix<Scalar, K + 1, K + 1> card_coeffmat()
{
  ::smooth::meta::StaticMatrix<Scalar, K + 1, K + 1> ret;
  if constexpr (K == 0) {
    ret[0][0] = 1;
    return ret;
  } else {
    constexpr auto coeff_mat_km1 = card_coeffmat<Scalar, K - 1>();
    ::smooth::meta::StaticMatrix<Scalar, K + 1, K> low, high;
    ::smooth::meta::StaticMatrix<Scalar, K, K + 1> left, right;

    for (std::size_t i = 0; i != K; ++i) {
      for (std::size_t j = 0; j != K; ++j) {
        low[i][j]      = coeff_mat_km1[i][j];
        high[i + 1][j] = coeff_mat_km1[i][j];
      }
    }

    for (std::size_t k = 0; k != K; ++k) {
      left[k][k + 1] = static_cast<Scalar>(K - (k + 1)) / static_cast<Scalar>(K);
      left[k][k]     = Scalar(1) - left[k][k + 1];

      right[k][k + 1] = Scalar(1) / static_cast<Scalar>(K);
      right[k][k]     = -right[k][k + 1];
    }

    return low * left + high * right;
  }
}

/**
 * @brief Calculate cardinal cumulative bspline coefficient matrix at compile-time
 * @tparam Scalar
 * @tparam K degree of bspline
 */
template<typename Scalar, std::size_t K>
constexpr ::smooth::meta::StaticMatrix<Scalar, K + 1, K + 1> cum_card_coeffmat()
{
  auto ret = card_coeffmat<Scalar, K>();
  for (std::size_t i = 0; i != K + 1; ++i) {
    for (std::size_t j = 0; j != K; ++j) { ret[i][K - 1 - j] += ret[i][K - j]; }
  }
  return ret;
}

}  // namespace detail

/**
 * @brief Evaluate a cardinal bspline of order K and calculate derivatives
 *
 *   g = g_0 * \Prod_{i=1}^{K} exp ( Btilde_i(u) * v_i )
 *
 * Where Btilde are cumulative Bspline basis functins.
 *
 * @tparam G lie group type
 * @tparam K bspline order
 * @tparam It iterator type
 * @param g_0 initial value
 * @param[in] diff_points range of differences v_i (must be of size K)
 * @param[in] u interval location: u = (t - ti) / dt \in [0, 1)
 * @param[out] vel calculate first order derivative w.r.t. u
 * @param[out] acc calculate second order derivative w.r.t. u
 * @param[out] der derivatives of g w.r.t. the K+1 control points
 */
template<std::size_t K, LieGroup G, std::ranges::range Range>
requires(std::is_same_v<std::ranges::range_value_t<Range>, typename G::Tangent>) G
  bspline_eval(const G & g_0,
    const Range & diff_points,
    typename G::Scalar u,
    std::optional<Eigen::Ref<typename G::Tangent>> vel                                        = {},
    std::optional<Eigen::Ref<typename G::Tangent>> acc                                        = {},
    std::optional<Eigen::Ref<Eigen::Matrix<typename G::Scalar, G::Dof, G::Dof *(K + 1)>>> der = {})
{
  if (std::ranges::size(diff_points) != K) {
    throw std::runtime_error("bspline: diff_points range must be size K=" + std::to_string(K)
                             + ", got " + std::to_string(std::ranges::size(diff_points)));
  }

  using Scalar = typename G::Scalar;
  Eigen::Matrix<Scalar, 1, K + 1> uvec, duvec, d2uvec;

  uvec(0)   = Scalar(1);
  duvec(0)  = Scalar(0);
  d2uvec(0) = Scalar(0);

  for (std::size_t k = 1; k != K + 1; ++k) {
    uvec(k) = u * uvec(k - 1);
    if (vel.has_value() || acc.has_value()) {
      duvec(k) = Scalar(k) * uvec(k - 1);
      if (acc.has_value()) { d2uvec(k) = Scalar(k) * duvec(k - 1); }
    }
  }

  // transpose to read rows that are consecutive in memory
  constexpr auto Ms = detail::cum_card_coeffmat<double, K>().transpose();

  Eigen::Matrix<Scalar, K + 1, K + 1> M =
    Eigen::Map<const Eigen::Matrix<double, K + 1, K + 1, Eigen::RowMajor>>(Ms[0].data())
      .template cast<Scalar>();

  if (vel.has_value() || acc.has_value()) {
    vel.value().setZero();
    if (acc.has_value()) { acc.value().setZero(); }
  }

  G g = g_0;
  for (std::size_t j = 1; const auto & v : diff_points) {
    const Scalar Btilde = uvec.dot(M.row(j));
    g *= G::exp(Btilde * v);

    if (vel.has_value() || acc.has_value()) {
      const Scalar dBtilde = duvec.dot(M.row(j));
      const auto Ad        = G::exp(-Btilde * v).Ad();
      vel.value().applyOnTheLeft(Ad);
      vel.value() += dBtilde * v;

      if (acc.has_value()) {
        const Scalar d2Btilde = d2uvec.dot(M.row(j));
        acc.value().applyOnTheLeft(Ad);
        acc.value() += dBtilde * G::ad(vel.value()) * v + d2Btilde * v;
      }
    }
    ++j;
  }

  if (der.has_value()) {
    G z2inv = G::Identity();

    der.value().setZero();

    // TODO: loop over diff_points instead of over differentiation variable to reduce calculations
    for (int j = K; j >= 0; --j) {
      if (j != K) {
        const Scalar Btilde_jp          = uvec.dot(M.row(j + 1));
        const typename G::Tangent & vjp = *(std::ranges::begin(diff_points) + j);
        const typename G::Tangent sjp   = Btilde_jp * vjp;

        der.value().template block<G::Dof, G::Dof>(0, j * G::Dof) -=
          Btilde_jp * z2inv.Ad() * G::dr_exp(sjp) * G::dl_expinv(vjp);
        z2inv *= G::exp(-sjp);
      }
      const Scalar Btilde_j          = uvec.dot(M.row(j));
      const typename G::Tangent & vj = *(std::ranges::begin(diff_points) + j - 1);

      der.value().template block<G::Dof, G::Dof>(0, j * G::Dof) +=
        Btilde_j * z2inv.Ad() * G::dr_exp(Btilde_j * vj) * G::dr_expinv(vj);
    }
  }

  return g;
}

/**
 * @brief Evaluate a cardinal bspline of order K and calculate derivatives
 *
 *   g = g_0 * \Prod_{i=1}^{K} exp ( Btilde_i(u) * log( g_{i-1}^{-1}  * g_i ) )
 *
 * Where Btilde are cumulative Bspline basis functins.
 *
 * @tparam G lie group type
 * @tparam K bspline order
 * @tparam It iterator type
 * @param[in] ctrl_points range of control points (must be of size K + 1)
 * @param[in] u interval location: u = (t - ti) / dt \in [0, 1)
 * @param[out] vel calculate first order derivative w.r.t. u
 * @param[out] acc calculate second order derivative w.r.t. u
 * @param[out] der derivatives w.r.t. the K+1 control points
 */
template<std::size_t K, LieGroup G, std::ranges::range Range>
requires(std::is_same_v<std::ranges::range_value_t<Range>, G>) G
  bspline_eval(const Range & ctrl_points,
    typename G::Scalar u,
    std::optional<Eigen::Ref<typename G::Tangent>> vel                                        = {},
    std::optional<Eigen::Ref<typename G::Tangent>> acc                                        = {},
    std::optional<Eigen::Ref<Eigen::Matrix<typename G::Scalar, G::Dof, G::Dof *(K + 1)>>> der = {})
{
  if (std::ranges::size(ctrl_points) != K + 1) {
    throw std::runtime_error("bspline: ctrl_points range must be size K+1=" + std::to_string(K + 1)
                             + ", got " + std::to_string(std::ranges::size(ctrl_points)));
  }

  auto b1 = std::begin(ctrl_points);
  auto b2 = std::begin(ctrl_points) + 1;

  std::array<typename G::Tangent, K> diff_pts;
  for (auto i = 0u; i != K; ++i) {
    diff_pts[i] = ((*b1).inverse() * (*b2)).log();
    ++b1;
    ++b2;
  }

  return bspline_eval<K, G>(*std::begin(ctrl_points), diff_pts, u, vel, acc, der);
}

template<std::size_t K, LieGroup G>
class BSpline {
public:
  /**
   * @brief Construct a cardinal bspline defined on [0, 1) with constant value
   */
  BSpline() : t0_(0), dt_(1), ctrl_pts_(K + 1, G::Identity()) {}

  /**
   * @brief Create a cardinal BSpline
   * @param t0 start of spline
   * @param dt end of spline
   * @param ctrl_pts spline control points
   *
   * The ctrl_pts - knot points correspondence is as follows
   *
   * KNOT  -K  -K+1   -K+2  ...    0    1   ...  N-K
   * CTRL   0     1      2  ...    K  K+1          N
   *                               ^               ^
   *                             t_min           t_max
   *
   * The first K ctrl_pts are exterior points and are outside
   * the support of the spline, which means that the spline is defined on
   * [t0_, (N-K)*dt]
   *
   * For interpolation purposes use an odd spline degree and set
   *
   *  t0 = (timestamp of first control point) + dt*K/2
   *
   * which aligns control points with the maximum of the corresponding
   * basis function.
   */
  BSpline(double t0, double dt, std::vector<G> && ctrl_pts)
      : t0_(t0), dt_(dt), ctrl_pts_(std::move(ctrl_pts))
  {
  }

  /**
   * @brief As above but for any range
   */
  template<std::ranges::range R>
  BSpline(double t0, double dt, const R & ctrl_pts)
  requires std::is_same_v<std::ranges::range_value_t<R>, G>
      : t0_(t0), dt_(dt), ctrl_pts_(std::ranges::begin(ctrl_pts), std::ranges::end(ctrl_pts))
  {
  }

  double t_min() const { return t0_; }

  double t_max() const { return t0_ + (ctrl_pts_.size() - K) * dt_; }

  G eval(double t,
    std::optional<Eigen::Ref<typename G::Tangent>> vel = {},
    std::optional<Eigen::Ref<typename G::Tangent>> acc = {}) const
  {
    // index of relevant interval
    int64_t istar = static_cast<int64_t>((t - t0_) / dt_);

    double u;
    // clamp to end of range if necessary
    if (istar < 0) {
      istar = 0;
      u     = 0;
    } else if (istar + K + 1 > ctrl_pts_.size()) {
      istar = ctrl_pts_.size() - K - 1;
      u     = 1;
    } else {
      u = (t - t0_ - istar * dt_) / dt_;
    }

    G g = bspline_eval<K, G>(
      ctrl_pts_ | std::views::drop(istar) | std::views::take(K + 1), u, vel, acc);

    if (vel.has_value()) { vel.value() /= dt_; }
    if (acc.has_value()) { acc.value() /= (dt_ * dt_); }

    return g;
  }

private:
  double t0_, dt_;
  std::vector<G> ctrl_pts_;
};

}  // namespace smooth

#endif  // INTERP__BSPLINE_HPP_
