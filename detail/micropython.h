#ifndef MICROPYTHON_WRAP_DETAIL_MICROPYTHON_H
#define MICROPYTHON_WRAP_DETAIL_MICROPYTHON_H

#ifdef _MSC_VER
#pragma warning ( disable : 4200 ) //nonstandard extension used : zero-sized array in struct/union
#endif
extern "C"
{
  #include <py/mpconfig.h>
  #include <py/misc.h>
  #include <py/qstr.h>
  #include <py/nlr.h>
  #include <py/obj.h>
  #include <py/objfun.h>
  #include <py/objmodule.h>
  #include <py/runtime.h>
  #include <py/runtime0.h>
  #include <py/mpz.h>
  #include <py/objint.h>
}
#ifdef _MSC_VER
#pragma warning ( default : 4200 )
#endif

#include <limits>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace upywrap
{
  inline mp_obj_t new_qstr( qstr what )
  {
    return MP_OBJ_NEW_QSTR( what );
  }

  inline mp_obj_t new_qstr( const char* what )
  {
    return new_qstr( qstr_from_str( what ) );
  }

  inline mp_obj_t MakeFunction( mp_obj_t (*fun) ( void ) )
  {
    auto o = m_new_obj( mp_obj_fun_builtin_fixed_t );
    o->base.type = &mp_type_fun_builtin_0;
    o->fun._0 = fun;
    return o;
  }

  inline mp_obj_t MakeFunction( mp_obj_t (*fun) ( mp_obj_t ) )
  {
    auto o = m_new_obj( mp_obj_fun_builtin_fixed_t );
    o->base.type = &mp_type_fun_builtin_1;
    o->fun._1 = fun;
    return o;
  }

  inline mp_obj_t MakeFunction( mp_obj_t (*fun) ( mp_obj_t, mp_obj_t ) )
  {
    auto o = m_new_obj( mp_obj_fun_builtin_fixed_t );
    o->base.type = &mp_type_fun_builtin_2;
    o->fun._2 = fun;
    return o;
  }

  inline mp_obj_t MakeFunction( mp_obj_t (*fun) ( mp_obj_t, mp_obj_t, mp_obj_t ) )
  {
    auto o = m_new_obj( mp_obj_fun_builtin_fixed_t );
    o->base.type = &mp_type_fun_builtin_3;
    o->fun._3 = fun;
    return o;
  }

  inline mp_obj_t MakeFunction( mp_uint_t numArgs, mp_obj_t (*fun) ( mp_uint_t, const mp_obj_t* ) )
  {
    auto o = m_new_obj( mp_obj_fun_builtin_var_t );
    o->base.type = &mp_type_fun_builtin_var;
    o->is_kw = false;
    o->n_args_min = numArgs;
    o->n_args_max = numArgs;
    o->fun.var = fun;
    return o;
  }

  //see mp_obj_fun_builtin_fixed_t: for up to 3 arguments there's a builtin function signature
  //this is reflected in MakeFunction
  //VS2013 hasn't constexpr yet so fall back to a macro..
  #define FitsBuiltinNativeFunction( numArgs ) ( (numArgs) < 4 )

  inline mp_obj_module_t* CreateModule( const char* name, bool doRegister = false )
  {
    const qstr qname = qstr_from_str( name );
    mp_obj_module_t* mod = (mp_obj_module_t*) mp_obj_new_module( qname );
    if( doRegister )
      mp_module_register( qname, mod );
    return mod;
  }

  inline void RaiseTypeException( const char* msg )
  {
    nlr_raise( mp_obj_new_exception_msg( &mp_type_TypeError, msg ) );
  }

  inline void RaiseTypeException( mp_const_obj_t source, const char* target )
  {
    nlr_raise( mp_obj_new_exception_msg_varg( &mp_type_TypeError, "can't convert %s to %s", mp_obj_get_type_str( source ), target ) );
  }

  inline void RaiseAttributeException( qstr name, qstr attr )
  {
    nlr_raise( mp_obj_new_exception_msg_varg( &mp_type_AttributeError, "'%s' object has no attribute '%s'", qstr_str( name ), qstr_str( attr ) ) );
  }

  inline mp_obj_t RaiseOverflowException( const char* msg )
  {
    nlr_raise( mp_obj_new_exception_msg( &mp_type_OverflowError, msg ) );
  }

  inline mp_obj_t RaiseRuntimeException( const char* msg )
  {
    nlr_raise( mp_obj_new_exception_msg( &mp_type_RuntimeError, msg ) );
  }

#ifdef UPYWRAP_NOEXCEPTIONS
  #define UPYWRAP_TRY
  #define UPYWRAP_CATCH
  inline bool HasExceptions()
  {
    return false;
  }
#else
  #define UPYWRAP_TRY try {
  #define UPYWRAP_CATCH } catch( const std::exception& e ) { return RaiseRuntimeException( e.what() ); }
  inline bool HasExceptions()
  {
    return true;
  }
#endif

  //Implement some casts used and check for overflow where trunctaion is needed.
  //Only implemented for conversions which are effectively used.
  template< class S, class T >
  struct safe_integer_caster
  {
  };

  template< class T >
  struct safe_integer_caster< T, T >
  {
    static T Convert( T src )
    {
      return src;
    }
  };

  template< class T, bool uns >
  struct abs_all
  {
    static T abs( T t )
    {
      return t;
    }
  };

  template< class T >
  struct abs_all< T, false >
  {
    static T abs( T t )
    {
      return std::abs( t );
    }
  };


#ifdef max
  #undef max
#endif

  template< class T, class S >
  static void IntegerBoundCheck( S src )
  {
    if( abs_all< S, std::is_unsigned< S >::value >::abs( src ) > static_cast< S >( std::numeric_limits< T >::max() ) )
      RaiseOverflowException( "Integer overflow" );
  }

  template< class T >
  static void PositiveIntegerCheck( T src )
  {
    if( src < 0 )
      RaiseTypeException( "Source integer must be unsigned" );
  }

  template<>
  struct safe_integer_caster< std::int64_t, int >
  {
    static int Convert( std::int64_t src )
    {
      IntegerBoundCheck< int >( src );
      return static_cast< int >( src );
    }
  };

  template<>
  struct safe_integer_caster< std::uint64_t, unsigned >
  {
    static unsigned Convert( std::uint64_t src )
    {
      IntegerBoundCheck< unsigned >( src );
      return static_cast< unsigned >( src );
    }
  };

  template<>
  struct safe_integer_caster< std::int32_t, std::uint32_t >
  {
    static std::uint32_t Convert( std::int32_t src )
    {
      PositiveIntegerCheck( src );
      return static_cast< std::uint32_t >( src );
    }
  };

  template<>
  struct safe_integer_caster< std::int32_t, std::uint64_t >
  {
    static std::uint64_t Convert( std::int32_t src )
    {
      PositiveIntegerCheck( src );
      return static_cast< std::uint64_t >( src );
    }
  };

  template<>
  struct safe_integer_caster< std::int64_t, std::uint64_t >
  {
    static std::uint64_t Convert( std::int64_t src )
    {
      PositiveIntegerCheck( src );
      return static_cast< std::uint64_t >( src );
    }
  };

  template<>
  struct safe_integer_caster< double, float >
  {
    static float Convert( double src )
    {
      IntegerBoundCheck< float >( src );
      return static_cast< float >( src );
    }
  };

  template< class T, class S >
  T safe_integer_cast( S src )
  {
    return safe_integer_caster< S, T >::Convert( src );
  }

}

#endif //#ifndef MICROPYTHON_WRAP_DETAIL_MICROPYTHON_H
