varying vec4 v_color;
varying vec2 v_texCoord;

uniform sampler2D tex0;

//Hardness of scanline.
//-8.0 = soft
//-16.0 = medium
float hardScan=-8.0;

//Hardness of pixels in scanline.
//-2.0 = soft
//-4.0 = hard
float hardPix=-3.0;

//Display warp.
//0.0 = none
//1.0/8.0 = extreme
vec2 warp = vec2(1.0/32.0, 1.0/24.0); 

//Amount of shadow mask.
float maskDark=0.5;
float maskLight=1.5;

vec2 iResolution = vec2(640.0, 480.0);

//------------------------------------------------------------------------
//sRGB to Linear.
//Assuming using sRGB typed textures this should not be needed.
float ToLinear1(float c){return(c<=0.04045)?c/12.92:pow((c+0.055)/1.055,2.4);}
vec3 ToLinear(vec3 c){return vec3(ToLinear1(c.r),ToLinear1(c.g),ToLinear1(c.b));}

//Linear to sRGB.
//Assuming using sRGB typed textures this should not be needed.
float ToSrgb1(float c)
{
	return (c < 0.0031308 ? c * 12.92 : 1.055 * pow(c, 0.41666) - 0.055);
}
vec3 ToSrgb(vec3 c)
{
	return vec3(ToSrgb1(c.r), ToSrgb1(c.g), ToSrgb1(c.b));
}

//Nearest emulated sample given floating point position and texel offset.
vec3 Fetch(vec2 pos,vec2 off)
{
	vec3 result;
	pos = (floor(pos * iResolution + off) + vec2(0.5, 0.5)) / iResolution;
	if (max(abs(pos.x-0.5), abs(pos.y-0.5)) > 0.5) return vec3(0);
	result = ToLinear(1.2 * texture2D(tex0 , pos.xy, -16.0).rgb);
	return result;
}

//Distance in emulated pixels to nearest texel.
vec2 Dist(vec2 pos)
{
	pos = pos * iResolution;
	return -((pos - floor(pos)) - vec2(0.5));
}

//1D Gaussian.
float Gauss(float pos, float scale)
{
	return exp2(scale * pos * pos);
}

//3-tap Gaussian filter along horz line.
vec3 Horz3(vec2 pos,float off)
{
	vec3 b = Fetch(pos,vec2(-1.0, off));
	vec3 c = Fetch(pos,vec2( 0.0, off));
	vec3 d = Fetch(pos,vec2( 1.0, off));
	float dst = Dist(pos).x;
	//Convert distance to weight.
	float scale = hardPix;
	float wb = Gauss(dst - 1.0, scale);
	float wc = Gauss(dst + 0.0, scale);
	float wd = Gauss(dst + 1.0, scale);
	//Return filtered sample.
	return (b * wb + c * wc + d * wd) / (wb + wc + wd);
}

//5-tap Gaussian filter along horz line.
vec3 Horz5(vec2 pos, float off)
{
	vec3 a = Fetch(pos,vec2(-2.0, off));
	vec3 b = Fetch(pos,vec2(-1.0, off));
	vec3 c = Fetch(pos,vec2( 0.0, off));
	vec3 d = Fetch(pos,vec2( 1.0, off));
	vec3 e = Fetch(pos,vec2( 2.0, off));
	float dst = Dist(pos).x;
	//Convert distance to weight.
	float scale = hardPix;
	float wa = Gauss(dst - 2.0, scale);
	float wb = Gauss(dst - 1.0, scale);
	float wc = Gauss(dst + 0.0, scale);
	float wd = Gauss(dst + 1.0, scale);
	float we = Gauss(dst + 2.0, scale);
	//Return filtered sample.
	return (a * wa + b * wb + c * wc + d * wd + e * we) / (wa + wb + wc + wd + we);
}

//Return scanline weight.
float Scan(vec2 pos,float off)
{
	float dst = Dist(pos).y;
	return Gauss(dst + off, hardScan);
}

//Allow nearest three lines to effect pixel.
vec3 Tri(vec2 pos)
{
	vec3 a = Horz3(pos,-1.0);
	vec3 b = Horz5(pos, 0.0);
	vec3 c = Horz3(pos, 1.0);
	float wa = Scan(pos,-1.0);
	float wb = Scan(pos, 0.0);
	float wc = Scan(pos, 1.0);
	return a * wa + b * wb + c * wc;
}

//Distortion of scanlines, and end of screen alpha.
vec2 Warp(vec2 pos)
{
	pos = pos * 2.0 - 1.0;    
	pos *= vec2(1.0 + (pos.y * pos.y) * warp.x, 1.0 + (pos.x * pos.x) * warp.y);
	return pos * 0.5 + 0.5;
}

vec3 Mask(vec2 pos)
{
	pos.x += pos.y * 3.0;
	vec3 mask = vec3(maskDark, maskDark, maskDark);
	pos.x = fract(pos.x / 6.0);
	if (pos.x < 0.333)
		mask.r = maskLight;
	else if (pos.x < 0.666)
		mask.g = maskLight;
	else
		mask.b = maskLight;
	return mask;
}

void main()
{
	vec2 pos = Warp(v_texCoord);
	vec4 result;
	result.rgb = Tri(pos) * Mask(gl_FragCoord.xy);
	gl_FragColor = v_color * vec4(ToSrgb(result.rgb), 1.0);
}
