"use strict";

///////
// CORE LIB //
     /////////
function foreach_do( arr, fn )
{
	for( var i = 0; i < arr.length; ++i )
		fn( arr[ i ] );
}
function find( selector, startNode )
{
	return document.querySelector( selector, startNode );
}
function findAll( selector, startNode )
{
	return document.querySelectorAll( selector, startNode );
}
function findID( id )
{
	return document.getElementById( id );
}
function bind( nodes, type, func )
{
	if( nodes instanceof Array )
	{
		for( var i = 0; i < nodes.length; ++i )
			bind( nodes[ i ], type, func );
		return;
	}
	nodes.addEventListener( type, func );
}
function empty( node )
{
	node.innerHTML = "";
	return node;
}
function is_attrib( nm )
{
	return (
		nm == "class" ||
		nm == "style" ||
		nm == "id"
	);
}
function element( type, attribs, ch )
{
	var el = document.createElement( type );
	if( attribs != null )
	{
		for( var key in attribs )
		{
			if( is_attrib( key ) )
				el.setAttribute( key, attribs[ key ] );
			else
				el[ key ] = attribs[ key ];
		}
	}
	if( ch instanceof Array )
	{
		for( var i = 0; i < ch.length; ++i )
		{
			el.appendChild( ch[ i ] );
		}
	}
	return el;
}
function goto_anchor( a )
{
	window.location.href = "#" + a;
}

/////
// DOCS //
   ///////
function map_toc()
{
	var path_info = {};
	for( var i = 0; i < sgs_toc.length; ++i )
	{
		var item = sgs_toc[ i ];
		var path = item[0];
		var at = path.lastIndexOf( ":" );
		var parent_path = at != -1 ? path.substring( 0, at ) : null;
		var alias = at != -1 ? path.substring( at + 1 ) : path;
		var data = { path: path, alias: alias, title: item[1], parent: parent_path, ch: [] };
		path_info[ path ] = data;
		if( at != -1 )
		{
			path_info[ parent_path ].ch.push( path );
			// also add the end-only version
			path_info[ alias ] = data;
		}
	}
	window.path_info = path_info;
}
function doc_create_entry( path )
{
	var item = window.path_info[ path ];
	var entry = element( "entry", { textContent: item.title, id: "entry:" + path, path: path } );
	if( item.ch.length )
	{
		var entries = [];
		for( var i = 0; i < item.ch.length; ++i )
			entries.push( doc_create_entry( item.ch[ i ] ) );
		return element( "entryset", { id: "entryset:" + path }, [entry, element( "entrych", null, entries )] );
	}
	return entry;
}
function doc_select_page( path )
{
	var info = path_info[ path ];
	goto_anchor( info.alias );
	path = info.path;
	foreach_do( findAll( "toc .active" ), function(e){ e.classList.remove( "active" ); } );
	findID( "entry:" + path ).classList.add( "active" );
	var title = find( "#view ptitle" );
	var cont = find( "#view pcont" );
	empty( title ).textContent = path_info[ path ].title;
	empty( cont ).appendChild( document.getElementById( path ).cloneNode(true) );
}
function doc_create_toc()
{
	var entries = [];
	for( var key in window.path_info )
	{
		var item = window.path_info[ key ];
		if( !item.parent )
			entries.push( doc_create_entry( key ) );
	}
	var entrylist;
	var out = element( "toc", null,
	[
		element( "header", null,
		[
			find( "logo" ),
			element( "subtitle", { textContent: find("title").textContent } ),
		]),
		element( "cont", null,
		[
			entrylist = element( "entrylist", null, entries ),
		]),
	]);
	bind( entrylist, "click", function(e)
	{
		if( e.target.tagName == "ENTRY" )
		{
			doc_select_page( e.target.path );
		}
	});
	return out;
}
function doc_create_view()
{
	var cont;
	var out = element( "view", { id: "view" },
	[
		element( "ptitle" ),
		cont = element( "pcont", { innerHTML: "<introtext>Click on a topic to view its contents</introtext>" } ),
	]);
	bind( cont, "click", function(e)
	{
		if( e.target.tagName == "A" )
		{
			var href = e.target.getAttribute( "href" );
			if( path_info[ href ] != null )
			{
				doc_select_page( href );
				e.preventDefault();
			}
		}
	});
	return out;
}
function doc_onhash()
{
	var hash = window.location.hash.substring( 1 );
	if( path_info[ hash ] != null )
	{
		doc_select_page( hash );
	}
	else
	{
		var loc = window.location.href, index = loc.indexOf('#');
		if( index != -1 )
			window.location = loc.substring( 0, index );
	}
}

bind( window, "load", function()
{
	map_toc();
	var frame = element( "docframe", null,
	[
		doc_create_toc(),
		doc_create_view(),
	]);
	empty( find("#_frame_") ).appendChild( frame );
	
	if( window.location.hash )
	{
		doc_onhash();
	}
});
bind( window, "hashchange", function()
{
	doc_onhash();
});
