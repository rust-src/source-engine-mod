//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "iclassmap.h"
#include "utldict.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class classentry_t
{
public:
	classentry_t()
	{
		mapname[ 0 ] = 0;
#ifdef LUA_SDK
		classname[ 0 ] = 0;
#endif
		factory = 0;
		size = -1;
#ifdef LUA_SDK
		scripted = false;
#endif
	}

	char const *GetMapName() const
	{
		return mapname;
	}

	void SetMapName( char const *newname )
	{
		Q_strncpy( mapname, newname, sizeof( mapname ) );
	}

#if defined ( LUA_SDK )
	char const *GetClassName() const
	{
		return classname;
	}

	void SetClassName( char const *newname )
	{
		Q_strncpy( classname, newname, sizeof( classname ) );
	}
#endif

	DISPATCHFUNCTION	factory;
	int					size;
#if defined ( LUA_SDK )
	bool				scripted;
#endif
private:
	char				mapname[ 40 ];
#if defined ( LUA_SDK )
	char				classname[ 40 ];
#endif
};

class CClassMap : public IClassMap
{
public:
#ifdef LUA_SDK
	virtual void			Add( const char *mapname, const char *classname, int size, DISPATCHFUNCTION factory /*= 0*/, bool scripted );
	virtual void			RemoveAllScripted( void );
#else
	virtual void			Add( const char *mapname, const char *classname, int size, DISPATCHFUNCTION factory /*= 0*/ );
#endif
	virtual const char		*Lookup( const char *classname );
#ifdef LUA_SDK
	virtual DISPATCHFUNCTION FindFactory( const char *classname );
#endif
	virtual C_BaseEntity	*CreateEntity( const char *mapname );
	virtual int				GetClassSize( const char *classname );

	// HL2SB: enumeration for SMenu (see iclassmap.h).  Ordinal-based so the
	// dictionary itself stays private.
	int						GetEntryCount( void );
	const char				*GetEntryNameByOrdinal( int iEntry );
	const char				*GetEntryCPPNameByOrdinal( int iEntry );
	bool					IsEntryScriptedByOrdinal( int iEntry );

private:
	CUtlDict< classentry_t, unsigned short > m_ClassDict;
};

IClassMap& GetClassMap( void )
{
	static CClassMap g_Classmap;
	return g_Classmap;
}

#ifdef LUA_SDK
void CClassMap::Add( const char *mapname, const char *classname, int size, DISPATCHFUNCTION factory = 0, bool scripted = false )
#else
void CClassMap::Add( const char *mapname, const char *classname, int size, DISPATCHFUNCTION factory = 0 )
#endif
{
#if defined ( LUA_SDK )
	for ( int i=m_ClassDict.First(); i != m_ClassDict.InvalidIndex(); i=m_ClassDict.Next( i ) )
	{
		classentry_t *lookup = &m_ClassDict[ i ];
		if ( !lookup )
			continue;

		if ( !Q_stricmp( lookup->GetMapName(), mapname ) )
		{
			m_ClassDict.RemoveAt( i );
		}
	}
#else
	const char *map = Lookup( classname );
	if ( map && !Q_strcasecmp( mapname, map ) )
		return;

	if ( map )
	{
		int index = m_ClassDict.Find( classname );
		Assert( index != m_ClassDict.InvalidIndex() );
		m_ClassDict.RemoveAt( index );
	}
#endif

	classentry_t element;
	element.SetMapName( mapname );
	element.factory = factory;
	element.size = size;
#if defined ( LUA_SDK )
	element.SetClassName( classname );
	element.scripted = scripted;
	m_ClassDict.Insert( mapname, element );
#else
	m_ClassDict.Insert( classname, element );
#endif
}

#ifdef LUA_SDK
void CClassMap::RemoveAllScripted( void )
{
	int c = m_ClassDict.Count();
	int i;

	for ( i = 0; i < c; i++ )
	{
		classentry_t *lookup = &m_ClassDict[ i ];
		if ( !lookup )
			continue;

		if ( lookup->scripted )
		{
			m_ClassDict.RemoveAt( i );
		}
	}
}
#endif

#if defined ( LUA_SDK )
const char *CClassMap::Lookup( const char *classname )
{
	int c = m_ClassDict.Count();
	int i;

	for ( i = 0; i < c; i++ )
	{
		classentry_t *lookup = &m_ClassDict[ i ];
		if ( !lookup )
			continue;

		if ( Q_stricmp( lookup->GetClassName(), classname ) )
			continue;

		return lookup->GetMapName();
	}

	return NULL;
}

DISPATCHFUNCTION CClassMap::FindFactory( const char *classname )
{
	for ( int i=m_ClassDict.First(); i != m_ClassDict.InvalidIndex(); i=m_ClassDict.Next( i ) )
	{
		classentry_t *lookup = &m_ClassDict[ i ];
		if ( !lookup )
			continue;

		if ( Q_stricmp( lookup->GetMapName(), classname ) )
			continue;

		return lookup->factory;
	}

	return NULL;
}
#else
const char *CClassMap::Lookup( const char *classname )
{
	unsigned short index;
	static classentry_t lookup; 

	index = m_ClassDict.Find( classname );
	if ( index == m_ClassDict.InvalidIndex() )
		return NULL;

	lookup = m_ClassDict.Element( index );
	return lookup.GetMapName();
}
#endif

C_BaseEntity *CClassMap::CreateEntity( const char *mapname )
{
	int c = m_ClassDict.Count();
	int i;

	for ( i = 0; i < c; i++ )
	{
		classentry_t *lookup = &m_ClassDict[ i ];
		if ( !lookup )
			continue;

		if ( Q_stricmp( lookup->GetMapName(), mapname ) )
			continue;

		if ( !lookup->factory )
		{
#if defined( _DEBUG )
			Msg( "No factory for %s/%s\n", lookup->GetMapName(), m_ClassDict.GetElementName( i ) );
#endif
			continue;
		}

		return ( *lookup->factory )();
	}

	return NULL;
}

int CClassMap::GetClassSize( const char *classname )
{
	int c = m_ClassDict.Count();
	int i;

	for ( i = 0; i < c; i++ )
	{
		classentry_t *lookup = &m_ClassDict[ i ];
		if ( !lookup )
			continue;

		if ( Q_strcmp( lookup->GetMapName(), classname ) )
			continue;

		return lookup->size;
	}

	return -1;
}

//-----------------------------------------------------------------------------
// HL2SB: SMenu enumeration (declared in iclassmap.h).
//
// Iteration is ordinal-based and walks the dictionary in key order - CUtlDict
// orders by CaselessStringLessThan, so the menu gets its entries alphabetically
// for free.  Only SMenu iterates (once per level / first open), so the O(n^2)
// walk of the ordinal accessors is not a hot path.
//-----------------------------------------------------------------------------
int CClassMap::GetEntryCount( void )
{
	return m_ClassDict.Count();
}

const char *CClassMap::GetEntryNameByOrdinal( int iEntry )
{
	int i = m_ClassDict.First();
	for ( int n = 0; i != m_ClassDict.InvalidIndex() && n < iEntry; ++n )
	{
		i = m_ClassDict.Next( i );
	}

	if ( i == m_ClassDict.InvalidIndex() )
		return NULL;

	return m_ClassDict.GetElementName( i );
}

const char *CClassMap::GetEntryCPPNameByOrdinal( int iEntry )
{
	int i = m_ClassDict.First();
	for ( int n = 0; i != m_ClassDict.InvalidIndex() && n < iEntry; ++n )
	{
		i = m_ClassDict.Next( i );
	}

	if ( i == m_ClassDict.InvalidIndex() )
		return NULL;

	return m_ClassDict[ i ].GetClassName();
}

bool CClassMap::IsEntryScriptedByOrdinal( int iEntry )
{
	int i = m_ClassDict.First();
	for ( int n = 0; i != m_ClassDict.InvalidIndex() && n < iEntry; ++n )
	{
		i = m_ClassDict.Next( i );
	}

	if ( i == m_ClassDict.InvalidIndex() )
		return false;

	return m_ClassDict[ i ].scripted;
}

int ClassMap_GetEntryCount( void )
{
	return static_cast< CClassMap * >( &GetClassMap() )->GetEntryCount();
}

const char *ClassMap_GetEntryName( int iEntry )
{
	return static_cast< CClassMap * >( &GetClassMap() )->GetEntryNameByOrdinal( iEntry );
}

const char *ClassMap_GetEntryCPPName( int iEntry )
{
	return static_cast< CClassMap * >( &GetClassMap() )->GetEntryCPPNameByOrdinal( iEntry );
}

bool ClassMap_IsEntryScripted( int iEntry )
{
	return static_cast< CClassMap * >( &GetClassMap() )->IsEntryScriptedByOrdinal( iEntry );
}
