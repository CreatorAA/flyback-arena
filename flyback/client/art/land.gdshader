shader_type spatial;
render_mode cull_disabled, specular_disabled;
uniform sampler2D ground_tex : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform vec2 field_size = vec2(18.,12.);
uniform vec3 rock_hi : source_color = vec3(.78,.62,.44);
uniform vec3 rock_lo : source_color = vec3(.44,.36,.46);
uniform vec3 lip : source_color = vec3(.47,.63,.33);
varying vec3 p;
varying float up;
void vertex(){p=VERTEX;up=NORMAL.y;}
void fragment(){
	float d=max(0.,-p.y);
	float stone_band=smoothstep(.40,1.8,d);
	float facets=.93+.095*sin(floor(p.x*2.1+sin(p.z)*.6)*4.2+floor(p.z*1.5)*1.7);
	vec3 stone=mix(rock_hi,rock_lo,stone_band*.88)*facets;
	stone=mix(lip,stone,smoothstep(.06,.22,d));
	float layer=.8+.10*sin(floor(p.x*1.25+p.z*.7)*2.);
	stone*=1.-.13*(1.-smoothstep(.015,.07,abs(d-layer)));
	float top=smoothstep(.56,.83,up)*(1.-smoothstep(.05,.16,d));
	vec3 grass=texture(ground_tex,(p.xz+vec2(.6))/(field_size+vec2(1.2))).rgb;
	float meadow=smoothstep(.02,.16,grass.g-grass.r);
	grass*=mix(vec3(.94,.91,.84),vec3(.81,.94,.82),meadow);
	ALBEDO=mix(stone*(.74-.19*stone_band),grass,top);
	ROUGHNESS=1.;
}
