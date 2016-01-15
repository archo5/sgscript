"use strict";

function byid( id ){ return document.getElementById( id ); }

window.addEventListener( "load", function()
{
	console.log(byid("_frame_"));
});
