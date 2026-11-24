shader_type spatial;
render_mode unshaded,cull_back;
varying float up;
void vertex(){up=NORMAL.y;}
void fragment(){
	float side=clamp(up*.65+.50,0.,1.);
	float facing=clamp(dot(normalize(NORMAL),normalize(VIEW)),0.,1.);
	ALBEDO=mix(vec3(.50,.68,.79),vec3(.86,.91,.92),side);
	ALPHA=.25+.59*smoothstep(.04,.71,facing);
}
