shader_type sky;
uniform vec3 high : source_color = vec3(.22,.49,.69);
uniform vec3 low : source_color = vec3(.48,.74,.82);
void sky(){
	// In an orthographic broadcast shot all view rays are parallel. Use the
	// screen height for aerial depth instead of an angle-dependent horizon.
	COLOR=mix(high,low,smoothstep(0.0,1.0,SCREEN_UV.y));
}
