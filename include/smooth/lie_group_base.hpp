#ifndef SMOOTH__LIE_GROUP_BASE_HPP_
#define SMOOTH__LIE_GROUP_BASE_HPP_

#include <cstdint>

#include "meta.hpp"
#include "storage.hpp"


namespace smooth
{

/**
 * @brief CRTP base for lie groups with common functionality and syntactic sugar
 */
template<typename Derived, int RepSize>
class LieGroupBase;

template<
  template<typename, typename, template<typename> typename ... _Ts> typename _Derived,
  typename _Scalar,
  typename _Storage,
  template<typename> typename ... _Ts,
  int RepSize
>
class LieGroupBase<_Derived<_Scalar, _Storage, _Ts...>, RepSize>
{
  using Scalar = _Scalar;
  using Storage = _Storage;
  using Derived = _Derived<Scalar, Storage, _Ts...>;
  using DerivedRet = _Derived<Scalar, DefaultStorage<Scalar, RepSize>, _Ts...>;

public:
  template<typename NewScalar>
  using CastType = _Derived<NewScalar, DefaultStorage<NewScalar, RepSize>, _Ts...>;

  /**
   * @brief Construct the group identity element
   */
  static DerivedRet Identity()
  {
    DerivedRet ret;
    ret.setIdentity();
    return ret;
  }

  /**
   * @brief Construct a random element
   *
   * Set the seed with std::srand(unsigned)
   */
  static DerivedRet Random()
  {
    DerivedRet ret;
    ret.setRandom();
    return ret;
  }

  /**
   * @brief Compare two Lie group elements
   */
  template<LieGroup OG>
  requires(std::is_same_v<_Scalar, typename OG::Scalar>)
  bool isApprox(
    const OG & o,
    const Scalar & eps = Eigen::NumTraits<Scalar>::dummy_precision()) const
  {
    double n1_sq{0}, n2_sq{0}, n12_sq{0};
    meta::static_for<RepSize>(
      [&](auto i) {
        n1_sq += coeffs()[i] * coeffs()[i];
        n2_sq += o.coeffs()[i] * o.coeffs()[i];
        n12_sq += (coeffs()[i] - o.coeffs()[i]) * (coeffs()[i] - o.coeffs()[i]);
      });
    return std::sqrt(n12_sq) <= eps * std::min(std::sqrt(n1_sq), std::sqrt(n2_sq));
  }

  /**
   * @brief Cast to different datatype
   */
  template<typename NewScalar>
  CastType<NewScalar> cast() const
  {
    CastType<NewScalar> ret;
    meta::static_for<RepSize>(
      [&](auto i) {
        ret.coeffs()[i] = static_cast<NewScalar>(static_cast<const Derived &>(*this).coeffs()[i]);
      });
    return ret;
  }

  /**
   * @brief Access group storage
   */
  Storage & coeffs()
  {
    return static_cast<Derived &>(*this).s_;
  }

  /**
   * @brief Const access group storage
   */
  const Storage & coeffs() const
  {
    return static_cast<const Derived &>(*this).s_;
  }

  /**
   * @brief Access raw data pointer
   */
  Scalar * data() requires ModifiableStorageLike<Storage>
  {
    return static_cast<Derived &>(*this).coeffs().data();
  }

  /**
   * @brief Access raw const data pointer
   */
  const Scalar * data() const requires MappableStorageLike<Storage>
  {
    return static_cast<const Derived &>(*this).coeffs().data();
  }

  /**
   * @brief Overload operator*= for inplace composition
   */
  template<StorageLike OS>
  Derived & operator*=(const _Derived<Scalar, OS, _Ts...> & o)
  requires ModifiableStorageLike<Storage>
  {
    static_cast<Derived &>(*this) = static_cast<const Derived &>(*this) * o;
    return static_cast<Derived &>(*this);
  }

  /**
   * @brief Overload operator+ for right-plus
   *
   * g + a := g1 * exp(a)
   */
  template<typename TangentDerived>
  auto operator+(const Eigen::MatrixBase<TangentDerived> & t) const
  {
    return static_cast<const Derived &>(*this) * Derived::exp(t);
  }

  /**
   * @brief Overload operator+= for inplace right-plus
   *
   * g + a := g1 * exp(a)
   */
  template<typename TangentDerived>
  Derived & operator+=(const Eigen::MatrixBase<TangentDerived> & t)
  {
    return static_cast<Derived &>(*this) *= Derived::exp(t);
  }

  /**
   * @brief Overload operator- for right-minus
   *
   * g1 - g2 := (g2.inverse() * g1).log()
   */
  template<StorageLike OS>
  auto operator-(const _Derived<Scalar, OS, _Ts...> & o) const
  {
    return (o.inverse() * static_cast<const Derived &>(*this)).log();
  }

  /**
   * @brief Left jacobian of the exponential
   */
  template<typename TangentDerived>
  static auto dl_exp(const Eigen::MatrixBase<TangentDerived> & t)
  {
    return (Derived::exp(t).Ad() * Derived::dr_exp(t)).eval();
  }

  /**
   * @brief Inverse of left jacobian of the exponential
   */
  template<typename TangentDerived>
  static auto dl_expinv(const Eigen::MatrixBase<TangentDerived> & t)
  {
    return (-Derived::ad(t) + Derived::dr_expinv(t)).eval();
  }

protected:
  // protect constructor to prevent direct instantiation
  LieGroupBase() = default;
};

}  // namespace smooth

template<typename Stream, typename Derived, int RepSize>
Stream & operator<<(Stream & s, const smooth::LieGroupBase<Derived, RepSize> & g)
{
  for (auto i = 0; i != RepSize; ++i) {
    s << g.coeffs()[i] << " ";
  }
  return s;
}

#endif  // SMOOTH__LIE_GROUP_BASE_HPP_
