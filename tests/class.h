#ifndef MICROPYTHON_WRAP_TESTS_CLASS_H
#define MICROPYTHON_WRAP_TESTS_CLASS_H

#include <iostream>
#include <memory>
#include <string>

namespace upywrap
{
  class Simple
  {
  public:
    Simple( int a ) :
      a( a )
    {
    }

    virtual ~Simple()
    {
    }

    void Add( int x )
    {
      a += x;
    }

    int Value() const
    {
      return a;
    }

    void SetValue( int x )
    {
      a = x;
    }

    void Plus( Simple& rh )
    {
      a += rh.a;
    }

    std::string Str() const
    {
      return "Simple " + std::to_string( a );
    }

    bool operator == ( const Simple& rh ) const
    {
      return a == rh.a;
    }

    bool operator != ( const Simple& rh ) const
    {
      std::cout << "__ne__" << std::endl;
      return !this->operator == ( rh );
    }

  private:
    int a;
  };

  Simple& SimpleFunc( Simple& p1, Simple& p2 )
  {
    p1.Plus( p2 );
    return p1;
  }

  class NewSimple : public Simple
  {
  public:
    NewSimple( int v ) :
      Simple( v )
    {
    }
  };

  NewSimple* ConstructNewSimple( int val )
  {
    return new NewSimple( val );
  }

  class SharedSimple : public Simple
  {
  public:
    SharedSimple( int v ) :
      Simple( v )
    {
    }
  };

  std::shared_ptr< SharedSimple > ConstructSharedSimple( int val )
  {
    return std::make_shared< SharedSimple >( val );
  }

  bool IsNullPtr( Simple* p )
  {
    return p == nullptr;
  }

  bool IsNullSharedPtr( std::shared_ptr< Simple > p )
  {
    return p == nullptr;
  }
}

#endif //#ifndef MICROPYTHON_WRAP_TESTS_CLASS_H
