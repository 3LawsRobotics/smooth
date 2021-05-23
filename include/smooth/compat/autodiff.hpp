#ifndef SMOOTH__COMPAT__AUTODIFF_HPP_
#define SMOOTH__COMPAT__AUTODIFF_HPP_

// clang-format off
#include <Eigen/Core>
#include <autodiff/forward/forward.hpp>
#include <autodiff/forward/eigen.hpp>
// clang-format on

#define SMOOTH_DIFF_AUTODIFF

#include "smooth/common.hpp"
#include "smooth/meta.hpp"

namespace smooth::diff {

/**
 * @brief Automatic differentiation in tangent space
 *
 * @param f function to differentiate
 * @param wrt... function arguments
 * @return pair( f(wrt...), dr f_(wrt...) )
 */
template<typename _F, typename... _Wrt>
auto dr_autodiff(_F && f, _Wrt &&... wrt)
{
  using Result   = std::invoke_result_t<_F, _Wrt...>;
  using Scalar   = typename Result::Scalar;
  using AdScalar = autodiff::forward::Dual<Scalar, Scalar>;

  static constexpr int Nx = std::min<int>({lie_info<std::decay_t<_Wrt>>::lie_dof...}) == -1
                            ? -1
                            : (lie_info<std::decay_t<_Wrt>>::lie_dof + ...);
  static constexpr int Ny = lie_info<Result>::lie_dof;

  auto val = f(wrt...);

  // tuple of zero-valued tangent elements
  std::tuple<Eigen::Matrix<AdScalar, lie_info<std::decay_t<_Wrt>>::lie_dof, 1>...> a_ad(
    Eigen::Matrix<AdScalar, lie_info<std::decay_t<_Wrt>>::lie_dof, 1>::Zero()...);

  // create tuple of references to members of a_ad
  auto a_ad_ref = std::apply(
    std::forward_as_tuple<Eigen::Matrix<AdScalar, lie_info<std::decay_t<_Wrt>>::lie_dof, 1> &...>,
    a_ad);

  Eigen::Matrix<Scalar, Ny, Nx> jac = autodiff::forward::jacobian(
    [&f, &val, &wrt...](auto &&... vars) -> Eigen::Matrix<AdScalar, Ny, 1> {
      return f(wrt.template cast<AdScalar>() + vars...) - val.template cast<AdScalar>();
    },
    a_ad_ref,
    a_ad_ref);

  return std::make_pair(val, jac);
}

}  // namespace smooth::diff

#endif  // SMOOTH__COMPAT__AUTODIFF_HPP_
