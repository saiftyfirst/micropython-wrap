#ifndef MICROPYTHON_WRAP_CLASSWRAPPER
#define MICROPYTHON_WRAP_CLASSWRAPPER

//This flags enables the use of shared_ptr instead of raw pointers
//for the actual storage of all native types.
//Per default this is on since passing around raw pointers quickly
//leads to undefined behavior when garbage collection comes into play.
//For example, consider this, where X and Y are both created With ClassWrapper,
//and Y::Store() takes an X* or X& and keeps a pointer to it internally:
//
//  y = Y()
//  y.Store( X() )
//  gc.collect()
//
//The last line will get rid of the ClassWrapper instance for the X object
//(since gc can't find a corresponding py object anymore as that was not stored;
//with actual py objects this wouldn't happen), so now y has a pointer to a deleted X.
//The only proper way around is using shared_ptr instead: if ClassWrapper has a
//shared_ptr< X >, Store takes a shared_ptr< X > (which it should do after all
//if it's planning to keep the argument longer then the function scope) and
//we pass a copy of ClassWrapper's object to Store, all is fine: when deleting
//the garbage collected object, shared_ptr's destructor is called but the object
//is not deleted unless there are no references anymore.
#ifndef UPYWRAP_SHAREDPTROBJ
  #define UPYWRAP_SHAREDPTROBJ (1)
#endif

//Require exact type matches when converting uPy objects into native ones.
//By default this is on in order to get proper error messages when passing mismatching types.
//However when your application wants to passs pointers to derived classes to functions
//taking base class pointers this has to be turned off.
#ifndef UPYWRAP_FULLTYPECHECK
  #define UPYWRAP_FULLTYPECHECK (1)
#endif

#include "detail/callreturn.h"
#include "detail/functioncall.h"
#include "detail/index.h"
#include "detail/util.h"
#include <cstdint>
#include <vector>
#if UPYWRAP_SHAREDPTROBJ
#include <memory>
#endif

namespace upywrap
{
  //Main logic for registering classes and their functions.
  //Usage:
  //
  //struct SomeClass
  //{
  //  SomeClass();
  //  int Foo( int );
  //  double Bar( const std::string& );
  //};
  //
  //struct Funcs
  //{
  //  func_name_def( Foo )
  //  func_name_def( Bar )
  //};
  //
  //ClassWrapper< SomeClass > wrap( "SomeClass", dict );
  //wrap.Def< Funcs::Foo >( &SomeClass::Foo );
  //wrap.Def< Funcs::Bar >( &SomeClass::Bar );
  //
  //This will register type "SomeClass" and given functions in dict,
  //so if dict is the global dict of a module "mod", the class
  //can be used in uPy like this:
  //
  //import mod;
  //x = mod.SomeClass();
  //x.Foo();
  //x.Bar();
  //
  //For supported arguments and return values see FromPyObj and ToPyObj classes.
  template< class T >
  class ClassWrapper
  {
  public:
#if UPYWRAP_SHAREDPTROBJ
    using native_obj_t = std::shared_ptr< T >;
#else
    using native_obj_t = T*;
#endif

    ClassWrapper( const char* name, mp_obj_module_t* mod ) :
      ClassWrapper( name, mod->globals )
    {
    }

    ClassWrapper( const char* name, mp_obj_dict_t* dict )
    {
      static bool init = false;
      if( !init )
      {
        OneTimeInit( name, dict );
        init = true;
      }
    }

    template< class A >
    void StoreClassVariable( const char* name, const A& value )
    {
      mp_obj_dict_store( MP_OBJ_FROM_PTR( type.locals_dict ), new_qstr( name ), ToPy( value ) );
    }

    template< index_type name, class Ret, class... A >
    void Def( Ret( *f ) ( T*, A... ), typename SelectRetvalConverter< Ret >::type conv = nullptr )
    {
      DefImpl< name, Ret, decltype( f ), A... >( f, conv );
    }

    template< index_type name, class Ret, class... A >
    void Def( Ret( *f ) ( T&, A... ), typename SelectRetvalConverter< Ret >::type conv = nullptr )
    {
      DefImpl< name, Ret, decltype( f ), A... >( f, conv );
    }

    template< index_type name, class Ret, class... A >
    void Def( Ret( *f ) ( const T&, A... ), typename SelectRetvalConverter< Ret >::type conv = nullptr )
    {
      DefImpl< name, Ret, decltype( f ), A... >( f, conv );
    }

    template< index_type name, class Ret, class... A >
    void Def( Ret( T::*f ) ( A... ), typename SelectRetvalConverter< Ret >::type conv = nullptr )
    {
      DefImpl< name, Ret, decltype( f ), A... >( f, conv );
    }

    template< index_type name, class Ret, class... A >
    void Def( Ret( T::*f ) ( A... ) const, typename SelectRetvalConverter< Ret >::type conv = nullptr )
    {
      DefImpl< name, Ret, decltype( f ), A... >( f, conv );
    }

    template< index_type name, class Base, class Ret, class... A >
    typename std::enable_if< std::is_base_of< Base, T >::value >::type Def( Ret( Base::*f ) ( A... ), typename SelectRetvalConverter< Ret >::type conv = nullptr )
    {
      DefImpl< name, Ret, decltype( f ), A... >( f, conv );
    }

    template< index_type name, class Base, class Ret, class... A >
    typename std::enable_if< std::is_base_of< Base, T >::value >::type Def( Ret( Base::*f ) ( A... ) const, typename SelectRetvalConverter< Ret >::type conv = nullptr )
    {
      DefImpl< name, Ret, decltype( f ), A... >( f, conv );
    }

    template< class A >
    void Setter( const char* name, void( *f )( T*, A ) )
    {
      SetterImpl< decltype( f ), A >( name, f );
    }

    template< class A >
    void Setter( const char* name, void( T::*f )( A ) )
    {
      SetterImpl< decltype( f ), A >( name, f );
    }

    template< class A >
    void Getter( const char* name, A( *f )( T* ) )
    {
      GetterImpl< decltype( f ), A >( name, f );
    }

    template< class A >
    void Getter( const char* name, A( T::*f )() const )
    {
      GetterImpl< decltype( f ), A >( name, f );
    }

    template< class A >
    void Property( const char* name, void( *fset )( T*, A ), A( *fget )( T* ) )
    {
      SetterImpl< decltype( fset ), A >( name, fset );
      GetterImpl< decltype( fget ), A >( name, fget );
    }

    template< class A >
    void Property( const char* name, void( T::*fset )( A ), A( T::*fget )() const )
    {
      SetterImpl< decltype( fset ), A >( name, fset );
      GetterImpl< decltype( fget ), A >( name, fget );
    }

    void DefInit()
    {
      DefInit<>();
    }

    template< class... A >
    void DefInit()
    {
      DefInit( ConstructorFactoryFunc< A... > );
    }

    template< class... A >
    void DefInit( T*( *f ) ( A... ) )
    {
      InitImpl< FixedFuncNames::Init, decltype( f ), T*, A... >( f );
    }

#if UPYWRAP_SHAREDPTROBJ
    template< class... A >
    void DefInit( std::shared_ptr< T >( *f ) ( A... ) )
    {
      InitImpl< FixedFuncNames::Init, decltype( f ), std::shared_ptr< T >, A... >( f );
    }
#endif

    void DefExit( void( T::*f ) () )
    {
      ExitImpl< FixedFuncNames::Exit, decltype( f ) >( f );
    }

#if UPYWRAP_SHAREDPTROBJ
    static mp_obj_t AsPyObj( T* p, bool own )
    {
      if( own )
      {
        return AsPyObj( native_obj_t( p ) );
      }
      return AsPyObj( native_obj_t( p, NoDelete ) );
    }
#else
    static mp_obj_t AsPyObj( T* p, bool )
    {
      return AsPyObj( p );
    }
#endif

    static mp_obj_t AsPyObj( native_obj_t p )
    {
      assert( p );
      CheckTypeIsRegistered();
      auto o = m_new_obj_with_finaliser( this_type );
      o->base.type = &type;
      o->cookie = defCookie;
      o->typeId = &typeid( T );
#if UPYWRAP_SHAREDPTROBJ
      new( &o->obj ) native_obj_t( std::move( p ) );
#else
      o->obj = p;
#endif
      return o;
    }

    static ClassWrapper< T >* AsNativeObjCheckedImpl( mp_obj_t arg )
    {
      auto native = (this_type*) MP_OBJ_TO_PTR( arg );
      if( !mp_obj_is_type( arg, &type ) )
      {
        //if whatever gets passed in doesn't remotely look like an object bail out
        //otherwise it's possible we're being passed an arbitrary 'opaque' ClassWrapper (so the cookie mathches)
        //which has not been registered or has been registered elsewhere (e.g. another dll, hence the uPy type check failure)
        //but if it's the same C++ type (or that check is disabled) we're good to go after all
        if( !mp_obj_is_obj( arg ) || native->cookie != defCookie
#if UPYWRAP_FULLTYPECHECK
            || typeid( T ) != *native->typeId
#endif
            )
        {
          return nullptr;
        }
      }
      return native;
    }

    static ClassWrapper< T >* AsNativeObjChecked( mp_obj_t arg )
    {
      if( auto native = AsNativeObjCheckedImpl( arg ) )
      {
        return native;
      }
      //Could be a Python class inheriting from us.
      //Can't use something like mp_obj_cast_to_native_base because that will try to compare
      //pointers to type and arg's type but those don't match if this_type is for a C++ class
      //which is a parent of arg's class i.e. this is ClassWrapper<A> and arg is ClassWrapper<B>
      //where B derives from A. Which also means this will never work if UPYWRAP_FULLTYPECHECK is
      //enabled, and if it's not you have to take care to only use this for types which actually
      //derive from each other else it's UB.
      if( mp_obj_is_obj( arg ) )
      {
        mp_obj_base_t* base = (mp_obj_base_t*) MP_OBJ_TO_PTR( arg );
        if( mp_obj_is_instance_type( base->type ) && base->type->parent != nullptr )
        {
          if( auto native = AsNativeObjCheckedImpl( ( (mp_obj_instance_t*) base )->subobj[ 0 ] ) )
          {
            return native;
          }
        }
      }
      CheckTypeIsRegistered(); //since we want to access type.name
      RaiseTypeException( arg, qstr_str( type.name ) );
#if !defined( _MSC_VER ) || defined( _DEBUG )
      return nullptr;
#endif
    }

    static T* AsNativeNonNullPtr( mp_obj_t arg )
    {
      return AsNativeObjChecked( arg )->GetPtr();
    }

    static T* AsNativePtr( mp_obj_t arg )
    {
      return arg == mp_const_none ? nullptr : AsNativeNonNullPtr( arg );
    }

    static native_obj_t AsNativeObj( mp_obj_t arg )
    {
      return arg == mp_const_none ? nullptr : AsNativeObjChecked( arg )->obj;
    }

#if UPYWRAP_SHAREDPTROBJ
    static native_obj_t& AsNativeObjRef( mp_obj_t arg ) //in case the native side wants a reference, avoid extra ptr copies
    {
      return AsNativeObjChecked( arg )->obj;
    }
#endif

  private:
    struct FixedFuncNames
    {
      func_name_def( Init )
      func_name_def( Exit )
    };

#if UPYWRAP_SHAREDPTROBJ
    template< class... Args >
    static std::shared_ptr< T > ConstructorFactoryFunc( Args... args )
    {
      return std::make_shared< T >( std::forward< Args >( args )... );
    }

    T* GetPtr()
    {
      return obj.get();
    }

    static void NoDelete( T* )
    {
    }
#else
    template< class... Args >
    static T* ConstructorFactoryFunc( Args... args )
    {
      return new T( std::forward< Args >( args )... );
    }

    T* GetPtr()
    {
      return obj;
    }
#endif

    //native attribute store interface
    struct NativeSetterCallBase
    {
      virtual void Call( mp_obj_t self_in, mp_obj_t value ) = 0;
    };

    //native attribute load interface
    struct NativeGetterCallBase
    {
      virtual mp_obj_t Call( mp_obj_t self_in ) = 0;
    };

    template< class Map >
    static typename Map::mapped_type FindAttrMaybe( Map& map, qstr attr )
    {
      auto ret = map.find( attr );
      if( ret == map.end() )
      {
        return nullptr;
      }
      return ret->second;
    }

    template< class Map >
    static typename Map::mapped_type FindAttrChecked( Map& map, qstr attr )
    {
      const auto attrValue = FindAttrMaybe( map, attr );
      if( !attrValue )
      {
        RaiseAttributeException( type.name, attr );
      }
      return attrValue;
    }

    static bool store_attr( mp_obj_t self_in, qstr attr, mp_obj_t value )
    {
      this_type* self = (this_type*) self_in;
      FindAttrChecked( self->setters, attr )->Call( self, value );
      return true;
    }

    static void load_attr( mp_obj_t self_in, qstr attr, mp_obj_t* dest )
    {
      //uPy calls load_attr to find methods as well, so we have no choice but to go through them.
      //However if we find one, it's more performant than uPy's lookup (see mp_load_method_maybe)
      //because we know we have a proper map with only functions so we don't need x checks
      auto locals_map = &( (mp_obj_dict_t*) type.locals_dict )->map;
      auto elem = mp_map_lookup( locals_map, new_qstr( attr ), MP_MAP_LOOKUP );
      if( elem != nullptr )
      {
        //assert( mp_obj_is_callable( elem->value ) )
        dest[ 0 ] = elem->value;
        dest[ 1 ] = self_in;
      }
      else
      {
        this_type* self = (this_type*) self_in;
        const auto attrValue = FindAttrMaybe( self->getters, attr );
        if( attrValue )
        {
          *dest = attrValue->Call( self );
        }
      }
    }

    static void attr( mp_obj_t self_in, qstr attr, mp_obj_t* dest )
    {
      if( dest[ 0 ] == MP_OBJ_NULL )
      {
        load_attr( self_in, attr, dest );
      }
      else
      {
        if( store_attr( self_in, attr, dest[ 1 ] ) )
        {
          dest[ 0 ] = MP_OBJ_NULL;
        }
      }
    }

    static mp_obj_t binary_op( mp_binary_op_t op, mp_obj_t self_in, mp_obj_t other_in )
    {
      auto self = (this_type*) self_in;
      auto other = (this_type*) other_in;
      if( op != MP_BINARY_OP_EQUAL )
      {
        return MP_OBJ_NULL; //not supported
      }
      return ToPy( self->GetPtr() == other->GetPtr() );
    }

    static void instance_print( const mp_print_t* print, mp_obj_t self_in, mp_print_kind_t kind )
    {
      auto self = (this_type*) self_in;
      mp_obj_t member[ 2 ] = { MP_OBJ_NULL };
      load_attr( self_in, ( kind == PRINT_STR ) ? MP_QSTR___str__ : MP_QSTR___repr__, member );
      if( member[ 0 ] == MP_OBJ_NULL && kind == PRINT_STR )
      {
        load_attr( self_in, MP_QSTR___repr__, member );  //fall back to __repr__ if __str__ not found
      }
      if( member[ 0 ] != MP_OBJ_NULL )
      {
        mp_obj_t r = mp_call_function_1( member[ 0 ], self_in );
        mp_obj_print_helper( print, r, PRINT_STR );
        return;
      }
      mp_printf( print, "<%s object at %p>", mp_obj_get_type_str( self_in ), self );
    }

    static mp_obj_t del( mp_obj_t self_in )
    {
      auto self = (this_type*) self_in;
#if UPYWRAP_SHAREDPTROBJ
      self->obj.~shared_ptr();
#else
      delete self->obj;
#endif
      return ToPyObj< void >::Convert();
    }

    void OneTimeInit( std::string name, mp_obj_dict_t* dict )
    {
      const auto qname = qstr_from_str( name.data() );
      type.base.type = &mp_type_type;
      type.name = static_cast< decltype( type.name ) >( qname );
      type.locals_dict = (mp_obj_dict_t*) mp_obj_new_dict( 0 );
      type.make_new = nullptr;
      type.attr = attr;
      type.binary_op = binary_op;
      type.unary_op = mp_generic_unary_op;
      type.print = instance_print;

      mp_obj_dict_store( dict, new_qstr( qname ), &type );
      //store our dict in the module's dict so it's reachable by the GC mark phase,
      //or in other words: prevent the GC from sweeping it!!
      mp_obj_dict_store( dict, new_qstr( ( name + "_locals" ).data() ), type.locals_dict );

      AddFunctionToTable( MP_QSTR___del__, MakeFunction( del ) );
    }

    static void CheckTypeIsRegistered()
    {
      if( type.base.type == nullptr )
      {
        RaiseTypeException( (std::string( "Native type " ) + typeid( T ).name() + " has not been registered").data() );
      }
    }

    void AddFunctionToTable( const qstr name, mp_obj_t fun )
    {
      mp_obj_dict_store( type.locals_dict, new_qstr( name ), fun );
    }

    void AddFunctionToTable( const char* name, mp_obj_t fun )
    {
      AddFunctionToTable( qstr_from_str( name ), fun );
    }

    template< index_type name, class Ret, class Fun, class... A >
    void DefImpl( Fun f, typename SelectRetvalConverter< Ret >::type conv )
    {
      typedef NativeMemberCall< name, Ret, A... > call_type;
      auto callerObject = call_type::CreateCaller( f );
      if( conv )
      {
        callerObject->convert_retval = conv;
      }
      functionPointers[ (void*) name ] = callerObject;
      AddFunctionToTable( name(), call_type::CreateUPyFunction() );
    }

    template< class Fun, class A >
    void SetterImpl( const char* name, Fun f )
    {
      setters[ qstr_from_str( name ) ] = new NativeSetterCall< A >( f );
    }

    template< class Fun, class A >
    void GetterImpl( const char* name, Fun f )
    {
      getters[ qstr_from_str( name ) ] = new NativeGetterCall< A >( f );
    }

    template< index_type name, class Fun, class Ret, class... A >
    void InitImpl( Fun f )
    {
      typedef NativeMemberCall< name, Ret, A... > call_type;
      functionPointers[ (void*) name ] = call_type::CreateCaller( f );
      type.make_new = call_type::MakeNew;
    }

    template< index_type name, class Fun >
    void ExitImpl( Fun f )
    {
      typedef NativeMemberCall< name, void > call_type;
      functionPointers[ (void*) name ] = call_type::CreateCaller( f );
      AddFunctionToTable( MP_QSTR___enter__, (mp_obj_t) &mp_identity_obj );
      AddFunctionToTable( MP_QSTR___exit__, MakeFunction( 4, call_type::CallDiscard ) );
    }

    //wrap native setter in function with uPy store_attr compatible signature
    template< class A >
    struct NativeSetterCall : NativeSetterCallBase
    {
      typedef InstanceFunctionCall< T, void, A > call_type;

      NativeSetterCall( typename call_type::func_type f ) :
        f( new NonMemberFunctionCall< T, void, A >( f ) )
      {
      }

      NativeSetterCall( typename call_type::mem_func_type f ) :
        f( new MemberFunctionCall< T, void, A >( f ) )
      {
      }

      void Call( mp_obj_t self_in, mp_obj_t value )
      {
        auto self = (this_type*) self_in;
        CallReturn< void, A >::Call( f, self->GetPtr(), value );
      }

    private:
      call_type* f;
    };

    //wrap native getter in function with uPy load_attr compatible signature
    template< class A >
    struct NativeGetterCall : NativeGetterCallBase
    {
      typedef InstanceFunctionCall< T, A > call_type;

      NativeGetterCall( typename call_type::func_type f ) :
        f( new NonMemberFunctionCall< T, A >( f ) )
      {
      }

      NativeGetterCall( typename call_type::const_mem_func_type f ) :
        f( new ConstMemberFunctionCall< T, A >( f ) )
      {
      }

      mp_obj_t Call( mp_obj_t self_in )
      {
        auto self = (this_type*) self_in;
        return CallReturn< A >::Call( f, self->GetPtr() );
      }

    private:
      call_type* f;
    };

    //wrap native call in function with uPy compatible mp_obj_t( mp_obj_t self, mp_obj_t.... ) signature
    template< index_type index, class Ret, class... A >
    struct NativeMemberCall
    {
      typedef InstanceFunctionCall< T, Ret, A... > call_type;
      typedef FunctionCall< Ret, A... > init_call_type;
      typedef typename call_type::func_type func_type;
      typedef typename call_type::byref_func_type byref_func_type;
      typedef typename call_type::byconstref_func_type byconstref_func_type;
      typedef typename call_type::mem_func_type mem_func_type;
      typedef typename call_type::const_mem_func_type const_mem_func_type;
      typedef typename init_call_type::func_type init_func_type;

      static call_type* CreateCaller( func_type f )
      {
        return new NonMemberFunctionCall< T, Ret, A... >( f );
      }

      static call_type* CreateCaller( byref_func_type f )
      {
        return new NonMemberByRefFunctionCall< T, Ret, A... >( f );
      }

      static call_type* CreateCaller( byconstref_func_type f )
      {
        return new NonMemberByConstRefFunctionCall< T, Ret, A... >( f );
      }

      static call_type* CreateCaller( mem_func_type f )
      {
        return new MemberFunctionCall< T, Ret, A... >( f );
      }

      static call_type* CreateCaller( const_mem_func_type f )
      {
        return new ConstMemberFunctionCall< T, Ret, A... >( f );
      }

      static init_call_type* CreateCaller( init_func_type f )
      {
        return new init_call_type( f );
      }

      static mp_obj_t CreateUPyFunction()
      {
        return CreateFunction< mp_obj_t, A... >::Create( Call, CallN );
      }

      static mp_obj_t CallDiscard( mp_uint_t n_args, const mp_obj_t* args )
      {
        assert( n_args == 4 );
        static_assert( sizeof...( A ) == 0, "Arguments must be discarded" );
        auto self = (this_type*) args[ 0 ];
        auto f = (call_type*) this_type::functionPointers[ (void*) index ];
        return CallReturn< Ret, A... >::Call( f, self->GetPtr() );
      }

      static mp_obj_t MakeNew( const mp_obj_type_t*, mp_uint_t n_args, mp_uint_t, const mp_obj_t *args )
      {
        if( n_args != sizeof...( A ) )
        {
          RaiseTypeException( "Wrong number of arguments for constructor" );
        }
        auto f = (init_call_type*) this_type::functionPointers[ (void*) index ];
        UPYWRAP_TRY
        return AsPyObj( native_obj_t( Apply( f, args, make_index_sequence< sizeof...( A ) >() ) ) );
        UPYWRAP_CATCH
      }

    private:
      static mp_obj_t Call( mp_obj_t self_in, typename project2nd< A, mp_obj_t >::type... args )
      {
        auto self = (this_type*) self_in;
        auto f = (call_type*) this_type::functionPointers[ (void*) index ];
        return CallReturn< Ret, A... >::Call( f, self->GetPtr(), args... );
      }

      static mp_obj_t CallN( mp_uint_t n_args, const mp_obj_t* args )
      {
        if( n_args != sizeof...( A ) + 1 )
        {
          RaiseTypeException( "Wrong number of arguments" );
        }
        auto self = (this_type*) args[ 0 ];
        auto firstArg = &args[ 1 ];
        auto f = (call_type*) this_type::functionPointers[ (void*) index ];
        return CallVar( f, self->GetPtr(), firstArg, make_index_sequence< sizeof...( A ) >() );
      }

      template< size_t... Indices >
      static Ret Apply( init_call_type* f, const mp_obj_t* args, index_sequence< Indices... > )
      {
        (void) args;
        return f->Call( FromPy< A >( args[ Indices ] )... );
      }

      template< size_t... Indices >
      static mp_obj_t CallVar( call_type* f, T* self, const mp_obj_t* args, index_sequence< Indices... > )
      {
        (void) args;
        return CallReturn< Ret, A... >::Call( f, self, args[ Indices ]... );
      }
    };

    typedef ClassWrapper< T > this_type;
    using store_attr_map = std::map< qstr, NativeSetterCallBase* >;
    using load_attr_map = std::map< qstr, NativeGetterCallBase* >;

    mp_obj_base_t base; //must always be the first member!
    std::int64_t cookie; //we'll use this to check if a pointer really points to a ClassWrapper
    const std::type_info* typeId; //and this will be used to check if types aren't being mixed
    native_obj_t obj;
    static mp_obj_type_t type;
    static function_ptrs functionPointers;
    static store_attr_map setters;
    static load_attr_map getters;
    static const std::int64_t defCookie;
  };

  template< class T >
  mp_obj_type_t ClassWrapper< T >::type =
#ifdef __GNUC__
    { { nullptr } }; //GCC bug 53119
#else
    { nullptr };
#endif

  template< class T >
  function_ptrs ClassWrapper< T >::functionPointers;

  template< class T >
  typename ClassWrapper< T >::store_attr_map ClassWrapper< T >::setters;

  template< class T >
  typename ClassWrapper< T >::load_attr_map ClassWrapper< T >::getters;

  template< class T >
  const std::int64_t ClassWrapper< T >::defCookie = 0x12345678908765;


  //Get instance pointer (or nullptr) out of mp_obj_t.
  template< class T >
  struct ClassFromPyObj< T* >
  {
    static T* Convert( mp_obj_t arg )
    {
      return ClassWrapper< T >::AsNativePtr( arg );
    }
  };

#if UPYWRAP_SHAREDPTROBJ
  template< class T >
  struct ClassFromPyObj< std::shared_ptr< T > >
  {
    static std::shared_ptr< T > Convert( mp_obj_t arg )
    {
      return ClassWrapper< T >::AsNativeObj( arg );
    }
  };

  template< class T >
  struct ClassFromPyObj< std::shared_ptr< T >& >
  {
    static std::shared_ptr< T >& Convert( mp_obj_t arg )
    {
      return ClassWrapper< T >::AsNativeObjRef( arg );
    }
  };
#endif

  template< class T >
  struct ClassFromPyObj< T& >
  {
    static T& Convert( mp_obj_t arg )
    {
      //make sure ClassFromPyObj< std::shared_ptr< T >& > gets used instead
      static_assert( !is_shared_ptr< T >::value, "cannot convert object to shared_ptr&" );
      return *ClassWrapper< T >::AsNativeNonNullPtr( arg );
    }
  };

  template< class T >
  struct ClassFromPyObj
  {
    static T Convert( mp_obj_t arg )
    {
      return *ClassWrapper< T >::AsNativeNonNullPtr( arg );
    }
  };

  template< class T >
  struct False : std::integral_constant< bool, false >
  {
  };

  //Wrap instance in a new mp_obj_t
  template< class T >
  struct ClassToPyObj
  {
    static mp_obj_t Convert( T )
    {
      static_assert( False< T >::value, "Conversion from value to ClassWrapper is not allowed, pass a reference or shared_ptr instead" );
      return mp_const_none;
    }
  };

  template< class T >
  struct ClassToPyObj< T* >
  {
    static mp_obj_t Convert( T* p )
    {
      //Could return ClassWrapper< T >::AsPyObj( p, false ) here, but if p was allocated
      //it wouldn't ever get deallocated so disallow this to avoid memory leaks
      static_assert( False< T >::value, "Storing bare pointers in ClassWrapper is not allowed, return a reference or shared_ptr instead" );
      return mp_const_none;
    }
  };

#if UPYWRAP_SHAREDPTROBJ
  template< class T >
  struct ClassToPyObj< std::shared_ptr< T > >
  {
    static mp_obj_t Convert( std::shared_ptr< T > p )
    {
      //Allow empty pointers, but don't convert them into ClassWrapper instances,
      //since trying to call functions on it would result in access violation anyway.
      //Return None instead which is either useful (e.g. used as 'optional' return value),
      //or will lead to an actual Python exception when used anyway.
      if( !p )
      {
        return mp_const_none;
      }
      return ClassWrapper< T >::AsPyObj( std::move( p ) );
    }
  };
#endif

  template< class T >
  struct ClassToPyObj< const T& >
  {
    static mp_obj_t Convert( const T& p )
    {
      static_assert( False< T >::value, "Conversion from const reference to ClassWrapper is not allowed since the const-ness cannot be guaranteed" );
      return mp_const_none;
    }
  };

  template< class T >
  struct ClassToPyObj< T& >
  {
    static mp_obj_t Convert( T& p )
    {
      //Make sure ClassToPyObj< std::shared_ptr< T > > gets used instead.
      static_assert( !is_shared_ptr< T >::value, "cannot convert object to shared_ptr&" );
      return ClassWrapper< T >::AsPyObj( &p, false );
    }
  };

  /**
    * Declare a bunch of common special method names.
    */
  struct special_methods
  {
    func_name_def( __str__ )
    func_name_def( __repr__ )
    func_name_def( __bytes__ )
    func_name_def( __format__ )
    func_name_def( __iter__ )
    func_name_def( __next__ )
    func_name_def( __reversed__ )
  };
}

//In order for native instances to be returned to uPy, they must have been registered.
//However sometimes you just want to return a native instance to another module without
//defining any class methods for use in uPy, then use this macro to quickly register the class.
//Note the other way around (passing uPy object as native instance into another module, where
//the native class is not registered) is not a problem, since in order to get a uPy object in
//the first place it obviously must have been registered already somewhere
#define UPYWRAP_REGISTER_OPAQUE( className, module ) \
  { \
    upywrap::ClassWrapper< className > registerInstance( MP_STRINGIFY( className ), module ); \
  }

#endif //#ifndef MICROPYTHON_WRAP_CLASSWRAPPER
