#version 430


/*Cosine lobe coeff*/
#define SH_cosLobe_C0 0.886226925f // sqrt(pi)/2 
#define SH_cosLobe_C1 1.02332671f // sqrt(pi/3) 

// SH_C0 * SH_cosLobe_C0 = 0.25000000007f
// SH_C1 * SH_cosLobe_C1 = 0.5000000011f

layout(location=0) out vec4 GeometryVolume;

uniform vec3 v_gridDim;
uniform vec3 v_lightPos;
uniform float f_cellSize;

in vec3 v_GSposFromRSM;
in vec3 v_GSnormalFromRSM;
in float GSsurfelArea;

//Should I normalize the dir vector?
vec4 evalCosineLobeToDir(vec3 dir) {
	dir = normalize(dir);
	//f00, f-11, f01, f11
	return vec4( SH_cosLobe_C0, -SH_cosLobe_C1 * dir.y, SH_cosLobe_C1 * dir.z, -SH_cosLobe_C1 * dir.x );
}

//(As * clamp(dot(ns,w),0.0,1.0))/(cellsize * cellsize)
float calculateBlockingPotencial(vec3 dir, vec3 normal) {
	//Without clamping to 0.0 - 1.0
	//return (GSsurfelArea * clamp(dot(normal,dir),0.0,1.0))/(f_cellSize*f_cellSize); 
	//Should I clamp it to 0.0 - 1.0? As long as it is probability I think so
	return clamp((GSsurfelArea * clamp(dot(normal,dir),0.0,1.0))/(f_cellSize*f_cellSize),0.0,1.0); //It is probability so 0.0 - 1.0
}

void main()
{

	//Discard pixels with really small normal
	if( length( v_GSnormalFromRSM ) < 0.01 ) {
		discard;
	}
	vec3 lightDir = normalize( v_lightPos - v_GSposFromRSM); //Both are in World space
	float blockingPotencial = calculateBlockingPotencial(lightDir, v_GSnormalFromRSM);

	vec4 SHCoeffGV = evalCosineLobeToDir(v_GSnormalFromRSM) * blockingPotencial;
	//GSsurfelArea
	//vec4 SHCoeffGV = evalCosineLobeToDir(v_GSnormalFromRSM) * GSsurfelArea;
	
	GeometryVolume = SHCoeffGV;
}