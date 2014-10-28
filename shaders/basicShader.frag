#version 430

//For MRT attachment 0
layout(location = 0) out vec4 final_color;
layout(location = 1) out vec4 normals;
in vec2 uv;
in vec3 eyePosition,eyeNormal,eyeLightPos;

uniform sampler2D texture;

void main()
{
	float distance = length(eyeLightPos.xyz-eyePosition.xyz);
	float att=1.0/(0.005+0.09*distance+0.01*distance*distance);

	vec3 ld = vec3(1.0,1.0,1.0);
	vec3 ls = vec3(1.0,1.0,1.0);
	vec3 la = vec3(1.0,1.0,1.0);

	vec3 kd = texture2D(texture, uv).rgb;
	vec3 ks = vec3(0.8,0.8,0.8);
	vec3 ka = vec3(0.1,0.1,0.1);
	vec3 tmpNormal = normalize(eyeNormal);
	vec3 s = normalize(vec3(eyeLightPos - eyePosition));
	vec3 v = normalize(-eyePosition);
	vec3 r = reflect (-s,tmpNormal); 
	float sDotN = max(dot(s, tmpNormal),0.0);
	vec3 diffuse = ld * kd * sDotN;
	vec3 spec = vec3(0.0);
	if( sDotN > 0.0) {
	    spec = ls * ks *pow(max(dot(r,v),0.0),8);
	}
	vec3 ambient = la * ka;
	vec3 lightIntesity =  (diffuse + spec)*att;
	final_color = vec4(lightIntesity,1.0);

	normals = vec4(tmpNormal,1.0);

    //final_color = texture2D(texture, uv);
	//final_color = vec4(1.0,0.0,0.0,1.0);
}