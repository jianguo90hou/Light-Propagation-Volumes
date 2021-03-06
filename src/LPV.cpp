/*
* File:   LPV.cpp
* Desc:		Hlavni soubor obsahujici vykreslovaci smycku
*
*/
#include "common.h"
#include "CTextureViewer.h"
#include "Grid.h"
#include "GLSLShader/GLSLShader.h"
#include "camera/controlCamera.h"
#include "Mesh/Mesh.h"
#include "fboManager.h"
#include "textureManager.h"
#include "CLightObject.h"
#include "texUtils.h"
#include "CBoundingBox.h"
#include "DebugDrawer.h"
#include "GBuffer.h"
#include "animationCamera.h"
#include "cubic.h"
#include "spline.h"
#include "CTimeQuery.h"


#ifdef _MSC_VER
#pragma comment(lib, "SDL2.lib")
#pragma comment(lib, "OpenGL32.lib")
#pragma comment(lib, "glew32.lib")

#pragma comment(lib,"devil.lib")
#pragma comment(lib,"ILU.lib")
//#pragma comment(lib,"ILUT.lib")

#pragma comment(lib,"assimp.lib")
/// etc
#endif

//error LNK2019: unresolved external symbol _main referenced in function ___tmainCRTStartup
#undef main
//or use sdlmain.lib

using namespace std;

#define WIDTH 800
#define HEIGHT 600

CTextureViewer * ctv;
CTextureViewer * ctv2;
CControlCamera * controlCamera = new CControlCamera();
GLSLShader basicShader, rsmShader, shadowMap, injectLight, injectLight_layered, VPLsDebug, geometryInject, geometryInject_layered, gBufferShader, propagationShader, propagationShader_layered;
Mesh * mesh;
GBuffer * gBuffer;
//glm::vec3 lightPosition(0.0, 4.0, 2.0);
CTextureManager texManager;
CFboManager * fboManager = new CFboManager();
CFboManager * RSMFboManager = new CFboManager();
CFboManager * ShadowMapManager = new CFboManager();
CLightObject * light;
DebugDrawer * dd, *dd_l1, *dd_l2;
//GLuint depthPassFBO;
GLint texture_units, max_color_attachments;
GLuint VPLsVAO, VPLsVBO, PropagationVAO, PropagationVBO;
glm::vec3 volumeDimensions, vMin, editedVolumeDimensions;

float aspect;
float movementSpeed = 10.0f;
float ftime;
float cellSize;
float f_tanFovXHalf;
float f_tanFovYHalf;
float f_texelAreaModifier = 1.0f; //Arbitrary value
float f_indirectAttenuation = 1.7f;
float initialCamHorAngle = 4.41052, initialCamVerAngle = -0.214501;

bool b_useNormalOffset = false;
bool b_firstPropStep = true;
bool b_useMultiStepPropagation  = true;
bool b_enableGI = true;
bool b_canWriteToFile = true;
bool b_lightIntesityOnly = false;
bool b_compileAndUseAtomicShaders = true;
bool b_firstFrame = true;
bool b_interpolateBorders = true;

bool b_recordingMode = false;
bool b_animation = true;

//bool b_recordingMode = true;
//bool b_animation = false;

bool b_profileMode = false;
bool b_showGrids = false;
bool b_useOcclusion = true;

//1
bool b_enableCascades = true;
bool b_useLayeredFill = true;
bool b_movableLPV = true;

//2
//bool b_enableCascades = true;
//bool b_useLayeredFill = false;
//bool b_movableLPV = true;

//3
//bool b_enableCascades = false;
//bool b_useLayeredFill = true;
//bool b_movableLPV = false;

//4
//bool b_enableCascades = false;
//bool b_useLayeredFill = false;
//bool b_movableLPV = false;

int volumeDimensionsMult;

Grid levels[CASCADES];
//v_allGridMins, v_allCellSizes
glm::vec3 v_allGridMins[CASCADES];
glm::vec3 v_allCellSizes;
glm::mat4 lastm0, lastm1, lastm2;

glm::mat4 biasMatrix(
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 0.5, 0.0,
	0.5, 0.5, 0.5, 1.0
	);

typedef struct propTex {
	GLuint red, green, blue;
} propTextureType;

int PROPAGATION_STEPS = 8;

propTextureType propTextures[CASCADES][MAX_PROPAGATION_STEPS];
propTextureType injectCascadeTextures[CASCADES];
propTextureType accumulatorCascadeTextures[CASCADES];
GLuint geometryInjectCascadeTextures[CASCADES];
CFboManager propagationFBOs[CASCADES][MAX_PROPAGATION_STEPS];
CFboManager lightInjectCascadeFBOs[CASCADES];
CFboManager geometryInjectCascadeFBOs[CASCADES];
glm::vec3 initialCameraPos = glm::vec3(31.4421, 21.1158, 3.80755);

int level_global = 0;
unsigned int currIndex = 0;

void printVector(glm::vec3 v);
void updateGrid();
std::fstream keyFrames;
std::fstream injectTimes, geometryInjectTimes, PropagationTimes, RSMTimes;
TimeQuery RSM,inject,geometry, propagation, finalLighting;

spline splinePath;
animationCamera * tmp;

/*
Push the quit event
*/
void kill() {
	SDL_Event event;
	event.type = SDL_QUIT;
	SDL_PushEvent(&event);
}

/*
Initializes VPL invocations
*/
void initializeVPLsInvocations() {
	////////////////////////////////////////////////////
	// VPL INIT STUFF
	////////////////////////////////////////////////////
	injectLight.Use();
	//Generate VAO
	glGenVertexArrays(1, &VPLsVAO);

	//Bind VAO
	glBindVertexArray(VPLsVAO);

	//Generate VBO
	glGenBuffers(1, &VPLsVBO);
	//Bind VBO
	glBindBuffer(GL_ARRAY_BUFFER, VPLsVBO);

	float *testPoints = new float[2 * VPL_COUNT];
	float step = 1.0 / VPL_COUNT;
	for (int i = 0; i < VPL_COUNT; ++i) {
		testPoints[i * 2] = 0.0f;
		testPoints[i * 2 + 1] = 0.0f;
	}

	//Alocate buffer
	glBufferData(GL_ARRAY_BUFFER, sizeof(testPoints), testPoints, GL_STATIC_DRAW);
	//Fill VBO
	//glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(testPoints), testPoints);

	//Fill attributes and uniforms
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (sizeof(float)* 2), (void*)0);

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glBindVertexArray(0);

	injectLight.UnUse();

	//Free memory
	delete testPoints;
}

/*
Initializes VAO for propagation
*/
void initializePropagationVAO(glm::vec3 volumeDimensions) {
	propagationShader.Use();

	//Generate VAO
	glGenVertexArrays(1, &PropagationVAO);

	//Bind VAO
	glBindVertexArray(PropagationVAO);

	//Generate VBO
	glGenBuffers(1, &PropagationVBO);
	//Bind VBO
	glBindBuffer(GL_ARRAY_BUFFER, PropagationVBO);

	volumeDimensionsMult = volumeDimensions.x * volumeDimensions.y * volumeDimensions.z;
	int x = volumeDimensions.x, y = volumeDimensions.y, z = volumeDimensions.z;
	/*float *testPoints = new float[3 * count];
	for (int i = 0; i < count; ++i) {
		testPoints[i * 3] = 0.0f;
		testPoints[i * 3 + 1] = 0.0f;
		testPoints[i * 3 + 2] = 0.0f;
	}*/

	std::vector<glm::vec3> coords;
	for (int d = 0; d < z; d++) {
		for (int c = 0; c < y; c++) {
			for (int r = 0; r < x; r++) {
				coords.push_back(glm::vec3((float)r, (float)c, (float)d));
			}
		}
	}

	//std::cout << coords.size() * 3 * sizeof(float) << std::endl;
	//std::cout << coords.size() * sizeof(glm::vec3) << std::endl;

	//Alocate buffer
	glBufferData(GL_ARRAY_BUFFER, coords.size() * 3 * sizeof(float), &coords.front(), GL_STATIC_DRAW);
	//Fill VBO
	//glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(testPoints), testPoints);

	//Fill attributes and uniforms
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (sizeof(float)* 3), (void*)0);

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glBindVertexArray(0);

	propagationShader.UnUse();
	//delete testPoints;

	/*for (int i = 0; i < coords.size(); i++) {
		std::cout << coords[i].x << " " << coords[i].y << " " << coords[i].z << std::endl;
	}*/
	//delete tmp;
}

//This function *MUST* be called after creation of injectCascadeTextures
void initPropStepTextures() {
	for (int l = 0; l < CASCADES; l++){
		propTextures[l][0].red = injectCascadeTextures[l].red;
		propTextures[l][0].green = injectCascadeTextures[l].green;
		propTextures[l][0].blue = injectCascadeTextures[l].blue;
		for (int i = 1; i < MAX_PROPAGATION_STEPS; i++) {
			string texNameR = "RLPVStep" + std::to_string(i) + "_cascade_" + std::to_string(l);
			string texNameG = "GLPVStep" + std::to_string(i) + "_cascade_" + std::to_string(l);
			string texNameB = "BLPVStep" + std::to_string(i) + "_cascade_" + std::to_string(l);
			//std::cout << texName << std::endl;
			texManager.createRGBA16F3DTexture(texNameR, volumeDimensions, GL_NEAREST, GL_CLAMP_TO_BORDER);
			texManager.createRGBA16F3DTexture(texNameG, volumeDimensions, GL_NEAREST, GL_CLAMP_TO_BORDER);
			texManager.createRGBA16F3DTexture(texNameB, volumeDimensions, GL_NEAREST, GL_CLAMP_TO_BORDER);
			propTextures[l][i].red = texManager[texNameR];
			propTextures[l][i].green = texManager[texNameG];
			propTextures[l][i].blue = texManager[texNameB];
		}
	}
}

//This function *MUST* be called after creation of accumulatorCascadeTextures
void initPropagationFBOs() {
	//for (int i = 1; i < PROPAGATION_STEPS; i++) {
	for (int l = 0; l < CASCADES; l++) {
		for (int i = 1; i < MAX_PROPAGATION_STEPS; i++) {

			propagationFBOs[l][i].initFbo();
			propagationFBOs[l][i].bind3DTextureToFbo(GL_COLOR_ATTACHMENT0, accumulatorCascadeTextures[l].red);
			propagationFBOs[l][i].bind3DTextureToFbo(GL_COLOR_ATTACHMENT1, accumulatorCascadeTextures[l].green);
			propagationFBOs[l][i].bind3DTextureToFbo(GL_COLOR_ATTACHMENT2, accumulatorCascadeTextures[l].blue);

			propagationFBOs[l][i].bind3DTextureToFbo(GL_COLOR_ATTACHMENT3, propTextures[l][i].red);
			propagationFBOs[l][i].bind3DTextureToFbo(GL_COLOR_ATTACHMENT4, propTextures[l][i].green);
			propagationFBOs[l][i].bind3DTextureToFbo(GL_COLOR_ATTACHMENT5, propTextures[l][i].blue);
			propagationFBOs[l][i].setDrawBuffers();
			if (!propagationFBOs[l][i].checkFboStatus()) {
				return;
			}
		}
	}

}

//Create textures & FBOs
void initInjectFBOs() {
	for (int i = 0; i < CASCADES; i++)
	{
		string texNameR = "LPVGridR_cascade_" + std::to_string(i);
		string texNameG = "LPVGridG_cascade_" + std::to_string(i);
		string texNameB = "LPVGridB_cascade_" + std::to_string(i);

		string texNameOcclusion = "GeometryVolume_cascade_" + std::to_string(i);

		string texNameRaccum = "RAccumulatorLPV_cascade_" + std::to_string(i);
		string texNameGaccum = "GAccumulatorLPV_cascade_" + std::to_string(i);
		string texNameBaccum = "BAccumulatorLPV_cascade_" + std::to_string(i);

		texManager.createRGBA16F3DTexture(texNameR, volumeDimensions, GL_NEAREST, GL_CLAMP_TO_BORDER);
		texManager.createRGBA16F3DTexture(texNameG, volumeDimensions, GL_NEAREST, GL_CLAMP_TO_BORDER);
		texManager.createRGBA16F3DTexture(texNameB, volumeDimensions, GL_NEAREST, GL_CLAMP_TO_BORDER);

		texManager.createRGBA16F3DTexture(texNameOcclusion, volumeDimensions, GL_LINEAR, GL_CLAMP_TO_BORDER);

		texManager.createRGBA16F3DTexture(texNameRaccum, volumeDimensions, GL_LINEAR, GL_CLAMP_TO_BORDER);
		texManager.createRGBA16F3DTexture(texNameGaccum, volumeDimensions, GL_LINEAR, GL_CLAMP_TO_BORDER);
		texManager.createRGBA16F3DTexture(texNameBaccum, volumeDimensions, GL_LINEAR, GL_CLAMP_TO_BORDER);

		injectCascadeTextures[i].red = texManager[texNameR];
		injectCascadeTextures[i].green = texManager[texNameG];
		injectCascadeTextures[i].blue = texManager[texNameB];

		geometryInjectCascadeTextures[i] = texManager[texNameOcclusion];

		accumulatorCascadeTextures[i].red = texManager[texNameRaccum];
		accumulatorCascadeTextures[i].green = texManager[texNameGaccum];
		accumulatorCascadeTextures[i].blue = texManager[texNameBaccum];

		lightInjectCascadeFBOs[i].initFbo();
		lightInjectCascadeFBOs[i].bind3DTextureToFbo(GL_COLOR_ATTACHMENT0, injectCascadeTextures[i].red);
		lightInjectCascadeFBOs[i].bind3DTextureToFbo(GL_COLOR_ATTACHMENT1, injectCascadeTextures[i].green);
		lightInjectCascadeFBOs[i].bind3DTextureToFbo(GL_COLOR_ATTACHMENT2, injectCascadeTextures[i].blue);

		lightInjectCascadeFBOs[i].setDrawBuffers();
		if (!lightInjectCascadeFBOs[i].checkFboStatus()) {
			return;
		}

		geometryInjectCascadeFBOs[i].initFbo();
		geometryInjectCascadeFBOs[i].bind3DTextureToFbo(GL_COLOR_ATTACHMENT0, geometryInjectCascadeTextures[i]);

		geometryInjectCascadeFBOs[i].setDrawBuffers();
		if (!geometryInjectCascadeFBOs[i].checkFboStatus()) {
			return;
		}
	}
}

/*
Check if extension is supported
*/
bool IsExtensionSupported(const char *name)
{
	GLint n = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &n);
	for (GLint i = 0; i < n; i++)
	{
		const char* extension =
			(const char*)glGetStringi(GL_EXTENSIONS, i);
		if (!strcmp(name, extension))
		{
			return true;
		}
	}
	return false;
}

/*
Init application
*/
void Initialize(SDL_Window * w) {
	//glGetIntegerv(GL_MAX_VERTEX_IMAGE_UNIFORMS, &texture_units);
	//Image uniforms GL_MAX_COMBINED_IMAGE_UNIFORMS - combined
	//glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &max_color_attachments);
	//std::cout << "Max texture units: " << texture_units << std::endl;
	//tex = loadImage("../textures/texture.png");
	if (!glewIsSupported("GL_ARB_clear_texture"))
	{
		std::cout << "GL_ARB_clear_texture not supported, using alternative method" << std::endl;
		texManager.setClearTextureExtension(false);
	}

	if (!IsExtensionSupported("GL_NV_shader_atomic_float")) {
		std::cout << "GL_NV_shader_atomic_float not supported, shaders using this extension won't be compiled" << std::endl;
		b_compileAndUseAtomicShaders = false;
	}

	if (!IsExtensionSupported("GL_NV_shader_atomic_fp16_vector")) {
		std::cout << "GL_NV_shader_atomic_fp16_vector not supported, shaders using this extension won't be compiled" << std::endl;
		b_compileAndUseAtomicShaders = false;
	}

	if (!IsExtensionSupported("GL_NV_gpu_shader5")) {
		std::cout << "GL_NV_gpu_shader5 not supported, shaders using this extension won't be compiled" << std::endl;
		b_compileAndUseAtomicShaders = false;
	}

	if (b_recordingMode) {
		string filename = "../misc/keyFrames.txt";
		keyFrames.open(filename, std::fstream::in | std::fstream::out | std::fstream::trunc);
	}

	if (b_profileMode) {
		string path = "../misc/";
		string gridSize = std::to_string(MAX_GRID_SIZE);
		string propagations = std::to_string(PROPAGATION_STEPS);
		string layered = (b_useLayeredFill) ? "layered" : "atomic";
		string cascaded = (b_enableCascades) ? "cascaded" : "single";
		string filename = path + "inject_" + gridSize + "_" + propagations + "_" + cascaded + "_" + layered + ".txt";
		injectTimes.open(filename, std::fstream::in | std::fstream::out | std::fstream::trunc);
		//filename = path + "geometryInject.txt";
		//geometryInjectTimes.open(filename, std::fstream::in | std::fstream::out | std::fstream::trunc);
		filename = path + "propagation_" + gridSize + "_" + propagations + "_" + cascaded + "_" + layered + ".txt";
		PropagationTimes.open(filename, std::fstream::in | std::fstream::out | std::fstream::trunc); 
		filename = path + "rsm.txt";
		RSMTimes.open(filename, std::fstream::in | std::fstream::out | std::fstream::trunc);
	}

	if (!keyFrames.is_open())
		b_canWriteToFile = false;

	//if (b_animation) {
		splinePath.init();
	//}

#ifdef ORTHO_PROJECTION
	/*
	Light POSITION vector: (-7.07759, 56.7856, 10.0773)
	Light DIRECTION vector: (0.168037, -0.964752, -0.202527)
	Light horizotnal angle: 2.449
	Light vertical angle: -1.3045

	Light POSITION vector: (-0.197587, 65.0856, 10.0773)
	Light DIRECTION vector: (0.000831289, -0.947236, -0.320536)
	Light horizotnal angle: 3.139
	Light vertical angle: -1.2445
	*/
	light = new CLightObject(glm::vec3(-0.197587, 65.0856, 10.0773), glm::vec3(0.000831289, -0.947236, -0.320536));
	light->setHorAngle(3.139);
	light->setVerAngle(-1.2445);
#else
	/*
	Light POSITION vector: (-0.977592, 59.4256, 10.0773)
	Light DIRECTION vector: (0.00145602, -0.827421, -0.56158)
	Light horizotnal angle: 3.139
	Light vertical angle: -0.9745
	*/
	light = new CLightObject(glm::vec3(-0.977592, 59.4256, 10.0773), glm::vec3(0.00145602, -0.827421, -0.56158));
	light->setHorAngle(3.139);
	light->setVerAngle(-0.9745);
#endif

	ctv = new CTextureViewer(0, "../shaders/textureViewer.vs", "../shaders/textureViewer.frag");
	ctv2 = new CTextureViewer(0, "../shaders/textureViewer.vs", "../shaders/textureViewer.frag");

	////////////////////////////////////////////////////
	// SHADERS INIT
	////////////////////////////////////////////////////
	basicShader.LoadFromFile(GL_VERTEX_SHADER, std::string("../shaders/basicShader.vs").c_str());
	basicShader.LoadFromFile(GL_FRAGMENT_SHADER, std::string("../shaders/basicShader.frag").c_str());
	basicShader.CreateAndLinkProgram();

	rsmShader.LoadFromFile(GL_VERTEX_SHADER, std::string("../shaders/RSMpass.vs").c_str());
	rsmShader.LoadFromFile(GL_FRAGMENT_SHADER, std::string("../shaders/RSMpass.frag").c_str());
	rsmShader.CreateAndLinkProgram();

	shadowMap.LoadFromFile(GL_VERTEX_SHADER, std::string("../shaders/depthOnly.vs").c_str());
	shadowMap.LoadFromFile(GL_FRAGMENT_SHADER, std::string("../shaders/depthOnly.frag").c_str());
	shadowMap.CreateAndLinkProgram();

	gBufferShader.LoadFromFile(GL_VERTEX_SHADER, std::string("../shaders/gbufferFill.vs").c_str());
	gBufferShader.LoadFromFile(GL_FRAGMENT_SHADER, std::string("../shaders/gbufferFill.frag").c_str());
	gBufferShader.CreateAndLinkProgram();

	if (b_compileAndUseAtomicShaders) {
		propagationShader.LoadFromFile(GL_VERTEX_SHADER, std::string("../shaders/propagation.vs").c_str());
		propagationShader.LoadFromFile(GL_FRAGMENT_SHADER, std::string("../shaders/propagation.frag").c_str());
		propagationShader.CreateAndLinkProgram();

		injectLight.LoadFromFile(GL_VERTEX_SHADER, std::string("../shaders/lightInject.vs").c_str());
		injectLight.LoadFromFile(GL_FRAGMENT_SHADER, std::string("../shaders/lightInject.frag").c_str());
		injectLight.CreateAndLinkProgram();

		geometryInject.LoadFromFile(GL_VERTEX_SHADER, std::string("../shaders/geometryInject.vs").c_str());
		geometryInject.LoadFromFile(GL_FRAGMENT_SHADER, std::string("../shaders/geometryInject.frag").c_str());
		geometryInject.CreateAndLinkProgram();
	}


	injectLight_layered.LoadFromFile(GL_VERTEX_SHADER, std::string("../shaders/lightInject_layered.vs").c_str());
	injectLight_layered.LoadFromFile(GL_GEOMETRY_SHADER, std::string("../shaders/lightInject_layered.gs").c_str());
	injectLight_layered.LoadFromFile(GL_FRAGMENT_SHADER, std::string("../shaders/lightInject_layered.frag").c_str());
	injectLight_layered.CreateAndLinkProgram();
	
	geometryInject_layered.LoadFromFile(GL_VERTEX_SHADER, std::string("../shaders/geometryInject_layered.vs").c_str());
	geometryInject_layered.LoadFromFile(GL_GEOMETRY_SHADER, std::string("../shaders/geometryInject_layered.gs").c_str());
	geometryInject_layered.LoadFromFile(GL_FRAGMENT_SHADER, std::string("../shaders/geometryInject_layered.frag").c_str());
	geometryInject_layered.CreateAndLinkProgram();
	
	propagationShader_layered.LoadFromFile(GL_VERTEX_SHADER, std::string("../shaders/propagation_layered.vs").c_str());
	propagationShader_layered.LoadFromFile(GL_GEOMETRY_SHADER, std::string("../shaders/propagation_layered.gs").c_str());
	propagationShader_layered.LoadFromFile(GL_FRAGMENT_SHADER, std::string("../shaders/propagation_layered.frag").c_str());
	propagationShader_layered.CreateAndLinkProgram();
	
#ifdef VPL_DEBUG
	VPLsDebug.LoadFromFile(GL_VERTEX_SHADER, std::string("../shaders/debugVPLs.vs").c_str());
	VPLsDebug.LoadFromFile(GL_FRAGMENT_SHADER, std::string("../shaders/debugVPLs.frag").c_str());
	VPLsDebug.CreateAndLinkProgram();
#endif

	////////////////////////////////////////////////////
	// CAMERA INIT
	////////////////////////////////////////////////////
	/*
	Camera POSITION vector: (31.4421, 21.1158, 3.80755)
	Camera UP vector: (-0.203285, 0.977082, -0.063123)
	Camera RIGHT vector: (0.296548, 0, -0.955018)
	Camera DIRECTION vector: (-0.932901, -0.21286, -0.290494)
	Camera horizotnal angle: 4.41052
	Camera vertical angle: -0.214501

	//Compare
	Camera POSITION vector: (35.7994, 4.02377, -0.424968)
	Camera UP vector: (-0.0519772, 0.998648, -0.000185819)
	Camera RIGHT vector: (0.00357499, 0, -0.999994)
	Camera DIRECTION vector: (-0.998639, -0.0519775, -0.00436523)
	Camera horizotnal angle: 4.70802
	Camera vertical angle: -0.052001

	Camera POSITION vector: (0.0695333, 17.7675, 2.18421)
	Camera UP vector: (-0.0157415, 0.999594, -0.0237547)
	Camera RIGHT vector: (0.833585, 0, -0.552392)
	Camera DIRECTION vector: (-0.551504, -0.0284971, -0.833686)
	Camera horizotnal angle: 3.72603
	Camera vertical angle: -0.0285009
	*/
	//Normal camera
	controlCamera->initControlCamera(glm::vec3(31.4421, 21.1158, 3.80755), w, 4.41052, -0.214501, WIDTH, HEIGHT, 1.0, 1000.0);
	//Compare
	//controlCamera->initControlCamera(glm::vec3(35.7994, 4.02377, -0.424968), w, 4.70802, -0.052001, WIDTH, HEIGHT, 1.0, 1000.0);
	//Grid compare
	//controlCamera->initControlCamera(glm::vec3(0.0695333, 17.7675, 2.18421), w, 3.72603, -0.0285009, WIDTH, HEIGHT, 1.0, 1000.0);

	////////////////////////////////////////////////////
	// UNIFORMS/ATTRIBUTES SETUP
	////////////////////////////////////////////////////
	basicShader.Use();
	basicShader.AddUniform("mvp");
	basicShader.AddUniform("mv");
	basicShader.AddUniform("v");
	basicShader.AddUniform("mn");
	basicShader.AddUniform("vLightPos");
	basicShader.AddUniform("shadowMatrix");
	basicShader.AddUniform("depthTexture");
	//basicShader.AddUniform("AccumulatorLPV");
	basicShader.AddUniform("RAccumulatorLPV_l0");
	basicShader.AddUniform("GAccumulatorLPV_l0");
	basicShader.AddUniform("BAccumulatorLPV_l0");
	basicShader.AddUniform("RAccumulatorLPV_l1");
	basicShader.AddUniform("GAccumulatorLPV_l1");
	basicShader.AddUniform("BAccumulatorLPV_l1");
	basicShader.AddUniform("RAccumulatorLPV_l2");
	basicShader.AddUniform("GAccumulatorLPV_l2");
	basicShader.AddUniform("BAccumulatorLPV_l2");
	basicShader.AddUniform("f_cellSize");
	basicShader.AddUniform("v_gridDim");
	basicShader.AddUniform("v_min");
	basicShader.AddUniform("f_indirectAttenuation");
	basicShader.AddUniform("b_enableGI");
	basicShader.AddUniform("b_enableCascades");
	basicShader.AddUniform("b_lightIntesityOnly");
	basicShader.AddUniform("b_interpolateBorders");
	basicShader.AddUniform("v_allGridMins");
	basicShader.AddUniform("v_allCellSizes");
	basicShader.UnUse();

	rsmShader.Use();
	rsmShader.AddUniform("mvp");
	rsmShader.AddUniform("m");
	rsmShader.AddUniform("mn");
	rsmShader.AddUniform("mv");
	rsmShader.AddUniform("v_lightPos");
	rsmShader.UnUse();

	shadowMap.Use();
	shadowMap.AddUniform("mvp");
	shadowMap.UnUse();

	gBufferShader.Use();
	gBufferShader.AddUniform("mvp");
	gBufferShader.AddUniform("mn");
	gBufferShader.AddUniform("mv");
	gBufferShader.AddUniform("colorTex");
	gBufferShader.UnUse();
	if (b_compileAndUseAtomicShaders) {
		injectLight.Use();
		injectLight.AddUniform("LPVGridR");
		injectLight.AddUniform("LPVGridG");
		injectLight.AddUniform("LPVGridB");
		injectLight.AddUniform("v_gridDim");
		injectLight.AddUniform("f_cellSize");
		injectLight.AddUniform("v_min");
		injectLight.AddUniform("i_RSMsize");
		injectLight.AddUniform("rsm_world_space_coords_tex");
		injectLight.AddUniform("rsm_normal_tex");
		injectLight.AddUniform("rsm_flux_tex");
		injectLight.UnUse();

		geometryInject.Use();
		geometryInject.AddUniform("GeometryVolume");
		geometryInject.AddUniform("v_gridDim");
		geometryInject.AddUniform("f_cellSize");
		geometryInject.AddUniform("v_min");
		geometryInject.AddUniform("i_RSMsize");
		geometryInject.AddUniform("rsm_world_space_coords_tex");
		geometryInject.AddUniform("rsm_normal_tex");
		geometryInject.AddUniform("rsm_flux_tex");
		geometryInject.AddUniform("f_tanFovXHalf");
		geometryInject.AddUniform("f_tanFovYHalf");
		geometryInject.AddUniform("v_lightPos");
		geometryInject.AddUniform("m_lightView");
		geometryInject.AddUniform("f_texelAreaModifier");
		geometryInject.UnUse();

		propagationShader.Use();
		//propagationShader.AddUniform("AccumulatorLPV");
		propagationShader.AddUniform("RAccumulatorLPV");
		propagationShader.AddUniform("GAccumulatorLPV");
		propagationShader.AddUniform("BAccumulatorLPV");
		propagationShader.AddUniform("GeometryVolume");
		propagationShader.AddUniform("RLightGridForNextStep");
		propagationShader.AddUniform("GLightGridForNextStep");
		propagationShader.AddUniform("BLightGridForNextStep");
		propagationShader.AddUniform("LPVGridR");
		propagationShader.AddUniform("LPVGridG");
		propagationShader.AddUniform("LPVGridB");
		propagationShader.AddUniform("b_firstPropStep");
		propagationShader.AddUniform("v_gridDim");
		propagationShader.AddUniform("b_useOcclusion");
		propagationShader.UnUse();
	}
	//b_useLayeredFill

	injectLight_layered.Use();
	injectLight_layered.AddUniform("LPVGridR");
	injectLight_layered.AddUniform("LPVGridG");
	injectLight_layered.AddUniform("LPVGridB");
	injectLight_layered.AddUniform("v_gridDim");
	injectLight_layered.AddUniform("f_cellSize");
	injectLight_layered.AddUniform("v_min");
	injectLight_layered.AddUniform("i_RSMsize");
	injectLight_layered.AddUniform("rsm_world_space_coords_tex");
	injectLight_layered.AddUniform("rsm_normal_tex");
	injectLight_layered.AddUniform("rsm_flux_tex");
	injectLight_layered.UnUse();

	geometryInject_layered.Use();
	geometryInject_layered.AddUniform("v_gridDim");
	geometryInject_layered.AddUniform("f_cellSize");
	geometryInject_layered.AddUniform("v_min");
	geometryInject_layered.AddUniform("i_RSMsize");
	geometryInject_layered.AddUniform("rsm_world_space_coords_tex");
	geometryInject_layered.AddUniform("rsm_normal_tex");
	geometryInject_layered.AddUniform("rsm_flux_tex");
	geometryInject_layered.AddUniform("f_tanFovXHalf");
	geometryInject_layered.AddUniform("f_tanFovYHalf");
	geometryInject_layered.AddUniform("v_lightPos");
	geometryInject_layered.AddUniform("m_lightView");
	geometryInject_layered.AddUniform("f_texelAreaModifier");
	geometryInject_layered.UnUse();
	
	propagationShader_layered.Use();
	//propagationShader.AddUniform("AccumulatorLPV");
	propagationShader_layered.AddUniform("GeometryVolume");
	propagationShader_layered.AddUniform("LPVGridR");
	propagationShader_layered.AddUniform("LPVGridG");
	propagationShader_layered.AddUniform("LPVGridB");
	propagationShader_layered.AddUniform("b_firstPropStep");
	propagationShader_layered.AddUniform("v_gridDim");
	propagationShader_layered.AddUniform("b_useOcclusion");
	propagationShader_layered.AddUniform("v_gridDim");
	propagationShader_layered.UnUse();

#ifdef VPL_DEBUG
	VPLsDebug.Use();
	VPLsDebug.AddUniform("rsm_world_space_coords_tex");
	VPLsDebug.AddUniform("rsm_normal_tex");
	VPLsDebug.AddUniform("mvp");
	VPLsDebug.AddUniform("i_RSMsize");
	VPLsDebug.AddUniform("b_useNormalOffset");
	VPLsDebug.UnUse();
#endif


	////////////////////////////////////////////////////
	// LOAD MODELS & FILL THE VARIABLES
	////////////////////////////////////////////////////
	mesh = new Mesh("../models/sponza_2.obj");
	levels[0] = mesh->getBoundingBox()->getGrid();
	volumeDimensions = levels[0].getDimensions();
	cellSize = levels[0].getCellSize();
	vMin = levels[0].getMin();

	CBoundingBox * bb_l0 = new CBoundingBox(levels[0].getMin(), levels[0].getMax());
	dd = new DebugDrawer(GL_LINE_STRIP, &(bb_l0->getDebugDrawPoints()), NULL, NULL, glm::vec3(1.0, 0.0, 0.0));
	delete bb_l0;

	if (CASCADES >= 3) {
		levels[1] = Grid(levels[0], 0.65,1);
		levels[2] = Grid(levels[1], 0.4,2);

		CBoundingBox * bb_l1 = new CBoundingBox(levels[1].getMin(), levels[1].getMax());
		CBoundingBox * bb_l2 = new CBoundingBox(levels[2].getMin(), levels[2].getMax());
		dd_l1 = new DebugDrawer(GL_LINE_STRIP, &(bb_l1->getDebugDrawPoints()), NULL, NULL, glm::vec3(0.0, 1.0, 0.0));
		dd_l2 = new DebugDrawer(GL_LINE_STRIP, &(bb_l2->getDebugDrawPoints()), NULL, NULL, glm::vec3(0.0, 0.0, 1.0));

		delete bb_l1;
		delete bb_l2;
	}

	initializeVPLsInvocations();
	initializePropagationVAO(volumeDimensions);
	initInjectFBOs();

	float f_lightFov = light->getFov(); //in degrees, one must convert to radians
	float f_lightAspect = light->getAspectRatio();

	f_tanFovXHalf = tanf(0.5 * f_lightFov * DEG2RAD);
	f_tanFovYHalf = tanf(0.5 * f_lightFov * DEG2RAD)*f_lightAspect; //Aspect is always 1, but just for sure

	gBuffer = new GBuffer(*(&texManager), WIDTH, HEIGHT);

	////////////////////////////////////////////////////
	// TEXTURE INIT
	////////////////////////////////////////////////////
	texManager.createTexture("render_tex", "", WIDTH, HEIGHT, GL_NEAREST, GL_RGBA16F, GL_RGBA, false);
	texManager.createTexture("rsm_normal_tex", "", RSMSIZE, RSMSIZE, GL_NEAREST, GL_RGBA16F, GL_RGBA, false);
	texManager.createTexture("rsm_world_space_coords_tex", "", RSMSIZE, RSMSIZE, GL_NEAREST, GL_RGBA16F, GL_RGBA, false);
	texManager.createTexture("rsm_flux_tex", "", RSMSIZE, RSMSIZE, GL_NEAREST, GL_RGBA16F, GL_RGBA, false);
	texManager.createTexture("rsm_depth_tex", "", SHADOWMAPSIZE, SHADOWMAPSIZE, GL_LINEAR, GL_DEPTH_COMPONENT32, GL_DEPTH_COMPONENT, true);
	
	initPropStepTextures();
	initPropagationFBOs();

	////////////////////////////////////////////////////
	// FBO INIT
	////////////////////////////////////////////////////
	fboManager->initFbo();
	fboManager->genRenderDepthBuffer(WIDTH, HEIGHT);
	fboManager->bindRenderDepthBuffer();
	fboManager->bindToFbo(GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texManager["render_tex"]);
	//fboManager->bindToFbo(GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texManager["rsm_normal_tex"]);
	//fboManager->bindToFbo(GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,texManager["rsm_depth_tex"]);
	fboManager->setDrawBuffers();
	if (!fboManager->checkFboStatus()){
		return;
	}

	RSMFboManager->initFbo();
	RSMFboManager->genRenderDepthBuffer(WIDTH, HEIGHT);
	RSMFboManager->bindRenderDepthBuffer();
	RSMFboManager->bindToFbo(GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texManager["rsm_world_space_coords_tex"]);
	RSMFboManager->bindToFbo(GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texManager["rsm_normal_tex"]);
	RSMFboManager->bindToFbo(GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, texManager["rsm_flux_tex"]);
	//RSMFboManager->bindToFbo(GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texManager["rsm_depth_tex"]);
	RSMFboManager->setDrawBuffers();
	if (!RSMFboManager->checkFboStatus()){
		return;
	}

	ShadowMapManager->initFbo();
	ShadowMapManager->bindToFbo(GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texManager["rsm_depth_tex"]);
	ShadowMapManager->setDrawBuffers();
	if (!ShadowMapManager->checkFboStatus()) {
		return;
	}

	//IN CASE OF PROBLEMS UNCOMMENT LINE BELOW
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/*
Propagation using atomic operations
*/
void propagate(int level) {
	glViewport(0, 0, volumeDimensions.x, volumeDimensions.y); //!! Set vieport to width and height of 3D texture!!
	glDisable(GL_DEPTH_TEST);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	propagationShader.Use();
	b_firstPropStep = true;

	vMin = levels[level].getMin();
	cellSize = levels[level].getCellSize();

	//GLfloat data[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	//glClearTexImage(texManager["AccumulatorLPV"], 0, GL_RGBA, GL_FLOAT, &data[0]);
	//texManager.clear3Dtexture(texManager["AccumulatorLPV"]);
	texManager.clear3Dtexture(accumulatorCascadeTextures[level].red);
	texManager.clear3Dtexture(accumulatorCascadeTextures[level].green);
	texManager.clear3Dtexture(accumulatorCascadeTextures[level].blue);
	//texManager.clear3Dtexture(propTextures[1]);

	glUniform1i(propagationShader("RAccumulatorLPV"), 0);
	glUniform1i(propagationShader("GAccumulatorLPV"), 1);
	glUniform1i(propagationShader("BAccumulatorLPV"), 2);

	glUniform1i(propagationShader("RLightGridForNextStep"), 3);
	glUniform1i(propagationShader("GLightGridForNextStep"), 4);
	glUniform1i(propagationShader("BLightGridForNextStep"), 5);

	//glUniform1i(propagationShader("LightGrid"), 3);
	//glUniform1i(propagationShader("LightGridForNextStep"), 4);

	//glUniform1i(propagationShader("GeometryVolume"), 5);
	glUniform1i(propagationShader("GeometryVolume"), 0);
	glUniform1i(propagationShader("LPVGridR"), 1);
	glUniform1i(propagationShader("LPVGridG"), 2);
	glUniform1i(propagationShader("LPVGridB"), 3);
	//glUniform1i(propagationShader("b_firstPropStep"), b_firstPropStep);
	glUniform3f(propagationShader("v_gridDim"), volumeDimensions.x, volumeDimensions.y, volumeDimensions.z);

	//glBindImageTexture(0, texManager["AccumulatorLPV"], 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
	glBindImageTexture(0, accumulatorCascadeTextures[level].red, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
	glBindImageTexture(1, accumulatorCascadeTextures[level].green, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
	glBindImageTexture(2, accumulatorCascadeTextures[level].blue, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
	//glBindImageTexture(5, texManager["GeometryVolume"], 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_3D, geometryInjectCascadeTextures[level]);

	if (b_useMultiStepPropagation ) {
		for (int i = 1; i < PROPAGATION_STEPS; i++) {
			//glUniform1i(propagationShader("AccumulatorLPV"), 0);
			if (i > 0)
				b_firstPropStep = false;
			glUniform1i(propagationShader("b_firstPropStep"), b_firstPropStep);
			glUniform1i(propagationShader("b_useOcclusion"), b_useOcclusion);
			//glBindImageTexture(3, propTextures[i-1], 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
			//glBindImageTexture(4, propTextures[i], 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_3D, propTextures[level][i - 1].red);
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_3D, propTextures[level][i - 1].green);
			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_3D, propTextures[level][i - 1].blue);

			glBindImageTexture(3, propTextures[level][i].red, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
			glBindImageTexture(4, propTextures[level][i].green, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
			glBindImageTexture(5, propTextures[level][i].blue, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);

			glBindVertexArray(PropagationVAO);
			glDrawArrays(GL_POINTS, 0, volumeDimensionsMult);
			glBindVertexArray(0);
		}

		for (int j = 1; j < PROPAGATION_STEPS; j++) {
			texManager.clear3Dtexture(propTextures[level][j].red);
			texManager.clear3Dtexture(propTextures[level][j].green);
			texManager.clear3Dtexture(propTextures[level][j].blue);
		}
	}
	else {
		//glBindImageTexture(3, propTextures[0], 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
		//glBindImageTexture(4, propTextures[1], 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, propTextures[level][0].red);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_3D, propTextures[level][0].green);
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_3D, propTextures[level][0].blue);

		glBindImageTexture(3, propTextures[level][1].red, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
		glBindImageTexture(4, propTextures[level][1].green, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
		glBindImageTexture(5, propTextures[level][1].blue, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);

		glUniform1i(propagationShader("b_firstPropStep"), b_firstPropStep);

		glBindVertexArray(PropagationVAO);
		glDrawArrays(GL_POINTS, 0, volumeDimensionsMult);
		glBindVertexArray(0);

		texManager.clear3Dtexture(propTextures[level][1].red);
		texManager.clear3Dtexture(propTextures[level][1].green);
		texManager.clear3Dtexture(propTextures[level][1].blue);
	}
	propagationShader.UnUse();
}

/*
Propagation using geometry shader
*/
void propagate_layered(int level) {
	glViewport(0, 0, volumeDimensions.x, volumeDimensions.y); //!! Set vieport to width and height of 3D texture!!
	glDisable(GL_DEPTH_TEST);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	propagationShader_layered.Use();
	b_firstPropStep = true;

	vMin = levels[level].getMin();
	cellSize = levels[level].getCellSize();

	texManager.clear3Dtexture(accumulatorCascadeTextures[level].red);
	texManager.clear3Dtexture(accumulatorCascadeTextures[level].green);
	texManager.clear3Dtexture(accumulatorCascadeTextures[level].blue);

	glUniform1i(propagationShader_layered("GeometryVolume"), 0);
	glUniform1i(propagationShader_layered("LPVGridR"), 1);
	glUniform1i(propagationShader_layered("LPVGridG"), 2);
	glUniform1i(propagationShader_layered("LPVGridB"), 3);
	glUniform3f(propagationShader_layered("v_gridDim"), volumeDimensions.x, volumeDimensions.y, volumeDimensions.z);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_3D, geometryInjectCascadeTextures[level]);

	if (b_useMultiStepPropagation ) {
		for (int i = 1; i < PROPAGATION_STEPS; i++) {
			if (i > 0)
				b_firstPropStep = false;
			glUniform1i(propagationShader_layered("b_firstPropStep"), b_firstPropStep);
			glUniform1i(propagationShader_layered("b_useOcclusion"), b_useOcclusion);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_3D, propTextures[level][i - 1].red);
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_3D, propTextures[level][i - 1].green);
			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_3D, propTextures[level][i - 1].blue);

			glBindFramebuffer(GL_FRAMEBUFFER, propagationFBOs[level][i].getFboId());
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE);
			//Additive
			glBlendEquation(GL_FUNC_ADD);

			glBindImageTexture(3, propTextures[level][i].red, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
			glBindImageTexture(4, propTextures[level][i].green, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
			glBindImageTexture(5, propTextures[level][i].blue, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);

			glBindVertexArray(PropagationVAO);
			glDrawArrays(GL_POINTS, 0, volumeDimensionsMult);
			glBindVertexArray(0);

			glDisable(GL_BLEND);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}

		for (int j = 1; j < PROPAGATION_STEPS; j++) {
			texManager.clear3Dtexture(propTextures[level][j].red);
			texManager.clear3Dtexture(propTextures[level][j].green);
			texManager.clear3Dtexture(propTextures[level][j].blue);
		}
	}
	else {
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, propTextures[level][0].red);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_3D, propTextures[level][0].green);
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_3D, propTextures[level][0].blue);

		glBindFramebuffer(GL_FRAMEBUFFER, propagationFBOs[level][1].getFboId());
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		//Additive
		glBlendEquation(GL_FUNC_ADD);

		glUniform1i(propagationShader_layered("b_firstPropStep"), b_firstPropStep);

		glBindVertexArray(PropagationVAO);
		glDrawArrays(GL_POINTS, 0, volumeDimensionsMult);
		glBindVertexArray(0);

		glDisable(GL_BLEND);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		texManager.clear3Dtexture(propTextures[level][1].red);
		texManager.clear3Dtexture(propTextures[level][1].green);
		texManager.clear3Dtexture(propTextures[level][1].blue);
	}
	propagationShader_layered.UnUse();
}

int inc = 1;

/*
Main drawing loop
*/
void Display() {
	//Clear the screen
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	//Clear color
	glClearColor(0.0, 0.0, 0.0, 1.0);
	//Enable depth testing
	glEnable(GL_DEPTH_TEST);
	//View port
	//glViewport(0, 0, WIDTH, HEIGHT);
	//downsample
	//glViewport(0,0,width/2,height/2);

	//Camera update
	glm::mat4 m = glm::mat4(1.0f);
	//m = glm::scale(m, glm::vec3(5.0f));
	//glm::mat4 m = glm::mat4(1.0f);
	glm::mat4 v, mvp, mv, vp, p;
	glm::mat3 mn;
	if (b_animation) {
		//std::cout << currIndex << "/" << splinePath.getSplineCameraPath().size() - 1 << std::endl;
		if (currIndex >= splinePath.getSplineCameraPath().size() - 1) {
			kill();
			return;
		}
		tmp = new animationCamera();
		tmp = splinePath.getSplineCameraPathOnIndex(currIndex);
		v = tmp->getAnimationCameraViewMatrix();
		p = tmp->getAnimationCameraProjectionMatrix();
		mn = glm::transpose(glm::inverse(glm::mat3(v*m)));
		mvp = p * v * m;
		mv = v * m;
		vp = p * v;
		//cout << inc << endl;
		currIndex += inc;
		//currIndex *= 2;


		//check end
	}
	else {
		controlCamera->computeMatricesFromInputs();
		v = controlCamera->getViewMatrix();
		p = controlCamera->getProjectionMatrix();
		mn = glm::transpose(glm::inverse(glm::mat3(v*m)));
		mvp = p * v * m;
		mv = v * m;
		vp = p * v;
	}



	glm::mat4 v_light = light->getViewMatrix();
	glm::mat4 p_light = light->getProjMatrix();
	glm::mat4 mvp_light = p_light * v_light * m;
	glm::mat4 inverse_vLight = glm::inverse(v_light);
	glm::mat3 mn_light = glm::transpose(glm::inverse(glm::mat3(v_light*m)));

	glm::vec3 lightPosition = light->getPosition();

	//Update grid
	if (b_movableLPV)
		updateGrid();
	/*
	////////////////////////////////////////////////////
	// FILL THE G-BUFFER
	////////////////////////////////////////////////////
	gBuffer->bindToRender();
	glViewport(0, 0, WIDTH, HEIGHT);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	gBufferShader.Use();
	glUniform1i(gBufferShader("colorTex"), 0);
	glUniformMatrix4fv(gBufferShader("mvp"), 1, GL_FALSE, glm::value_ptr(mvp));
	glUniformMatrix4fv(gBufferShader("mv"), 1, GL_FALSE, glm::value_ptr(mv));
	glUniformMatrix3fv(gBufferShader("mn"), 1, GL_FALSE, glm::value_ptr(mn));
	mesh->render();
	gBufferShader.UnUse();
	gBuffer->unbind();
	*/

	////////////////////////////////////////////////////
	// SHADOW MAP
	////////////////////////////////////////////////////
	//glEnable(GL_CULL_FACE);
	//glCullFace(GL_FRONT);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1, 1);

	glBindFramebuffer(GL_FRAMEBUFFER, ShadowMapManager->getFboId());
	shadowMap.Use();
	glViewport(0, 0, SHADOWMAPSIZE, SHADOWMAPSIZE);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	light->computeMatrixes();
	glUniformMatrix4fv(shadowMap("mvp"), 1, GL_FALSE, glm::value_ptr(mvp_light));
	mesh->render();
	shadowMap.UnUse();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glDisable(GL_POLYGON_OFFSET_FILL);
	//glDisable(GL_CULL_FACE);
	if (b_enableGI) {
		////////////////////////////////////////////////////
		// RSM
		////////////////////////////////////////////////////
		RSM.start();
		glBindFramebuffer(GL_FRAMEBUFFER, RSMFboManager->getFboId());
		rsmShader.Use();
		glViewport(0, 0, RSMSIZE, RSMSIZE);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		light->computeMatrixes();
		glUniformMatrix4fv(rsmShader("mvp"), 1, GL_FALSE, glm::value_ptr(mvp_light));
		//glUniformMatrix4fv(rsmShader("mv"), 1, GL_FALSE, glm::value_ptr(v_light));
		glUniformMatrix4fv(rsmShader("m"), 1, GL_FALSE, glm::value_ptr(m));
		glUniform3f(rsmShader("v_lightPos"), lightPosition.x, lightPosition.y, lightPosition.z);
		//glUniformMatrix3fv(rsmShader("mn"), 1, GL_FALSE, glm::value_ptr(mn_light));
		mesh->render();
		rsmShader.UnUse();
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		RSM.stop();
		//std::cout << testQuery.getElapsedTime() << std::endl;
		////////////////////////////////////////////////////
		// LIGHT INJECT
		////////////////////////////////////////////////////
		//texManager.clear3Dtexture(texManager["LPVGridR"]);
		//texManager.clear3Dtexture(texManager["LPVGridG"]);
		//texManager.clear3Dtexture(texManager["LPVGridB"]);

		int end = 1;

		if (b_enableCascades)
			end = CASCADES;
		inject.start();
		glViewport(0, 0, volumeDimensions.x, volumeDimensions.y); //!! Set vieport to width and height of 3D texture!!
		glDisable(GL_DEPTH_TEST);
		glClearColor(0.0, 0.0, 0.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		for (int i = 0; i < end; i++) {
			texManager.clear3Dtexture(injectCascadeTextures[i].red);
			texManager.clear3Dtexture(injectCascadeTextures[i].green);
			texManager.clear3Dtexture(injectCascadeTextures[i].blue);

			vMin = levels[i].getMin();
			cellSize = levels[i].getCellSize();

			if (b_useLayeredFill) {

				glBindFramebuffer(GL_FRAMEBUFFER, lightInjectCascadeFBOs[i].getFboId());
				glEnable(GL_BLEND);
				glBlendFunc(GL_ONE, GL_ONE);
				//Additive
				glBlendEquation(GL_FUNC_ADD);
				injectLight_layered.Use();

				glUniform1i(injectLight_layered("rsm_world_space_coords_tex"), 0);
				glUniform1i(injectLight_layered("rsm_normal_tex"), 1);
				glUniform1i(injectLight_layered("rsm_flux_tex"), 2);
				glUniform1i(injectLight_layered("i_RSMsize"), RSMSIZE);
				glUniform1f(injectLight_layered("f_cellSize"), cellSize);
				glUniform3f(injectLight_layered("v_gridDim"), volumeDimensions.x, volumeDimensions.y, volumeDimensions.z);
				glUniform3f(injectLight_layered("v_min"), vMin.x, vMin.y, vMin.z);
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, texManager["rsm_world_space_coords_tex"]);
				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, texManager["rsm_normal_tex"]);
				glActiveTexture(GL_TEXTURE2);
				glBindTexture(GL_TEXTURE_2D, texManager["rsm_flux_tex"]);

				glBindVertexArray(VPLsVAO);//aktivujeme VAO
				glDrawArrays(GL_POINTS, 0, VPL_COUNT);
				glBindVertexArray(0);//deaktivujeme VAO
				injectLight_layered.UnUse();
				glDisable(GL_BLEND);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
			}
			else {
				if (b_compileAndUseAtomicShaders) {
					injectLight.Use();
					//texManager.clear3Dtexture(texManager["LPVGridR"]);
					//texManager.clear3Dtexture(texManager["LPVGridG"]);
					//texManager.clear3Dtexture(texManager["LPVGridB"]);

					glUniform1i(injectLight("LPVGridR"), 0);
					glUniform1i(injectLight("LPVGridG"), 1);
					glUniform1i(injectLight("LPVGridB"), 2);
					glUniform1i(injectLight("rsm_world_space_coords_tex"), 0);
					glUniform1i(injectLight("rsm_normal_tex"), 1);
					glUniform1i(injectLight("rsm_flux_tex"), 2);
					glUniform1i(injectLight("i_RSMsize"), RSMSIZE);
					glUniform1f(injectLight("f_cellSize"), cellSize);
					glUniform3f(injectLight("v_gridDim"), volumeDimensions.x, volumeDimensions.y, volumeDimensions.z);
					glUniform3f(injectLight("v_min"), vMin.x, vMin.y, vMin.z);
					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, texManager["rsm_world_space_coords_tex"]);
					glActiveTexture(GL_TEXTURE1);
					glBindTexture(GL_TEXTURE_2D, texManager["rsm_normal_tex"]);
					glActiveTexture(GL_TEXTURE2);
					glBindTexture(GL_TEXTURE_2D, texManager["rsm_flux_tex"]);
					glBindImageTexture(0, injectCascadeTextures[i].red, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
					glBindImageTexture(1, injectCascadeTextures[i].green, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
					glBindImageTexture(2, injectCascadeTextures[i].blue, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
					glBindVertexArray(VPLsVAO);
					glDrawArrays(GL_POINTS, 0, VPL_COUNT);
					glBindVertexArray(0);
					injectLight.UnUse();
				}
			}

			////////////////////////////////////////////////////
			// GEOMETRY INJECT
			////////////////////////////////////////////////////
			glViewport(0, 0, volumeDimensions.x, volumeDimensions.y); //!! Set vieport to width and height of 3D texture!!
			glDisable(GL_DEPTH_TEST);
			glClearColor(0.0, 0.0, 0.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			texManager.clear3Dtexture(geometryInjectCascadeTextures[i]);
			if (b_useLayeredFill) {
				glBindFramebuffer(GL_FRAMEBUFFER, geometryInjectCascadeFBOs[i].getFboId());
				glEnable(GL_BLEND);
				glBlendFunc(GL_ONE, GL_ONE);
				//Additive
				glBlendEquation(GL_FUNC_ADD);
				geometryInject_layered.Use();
				glUniform1i(geometryInject_layered("rsm_world_space_coords_tex"), 0);
				glUniform1i(geometryInject_layered("rsm_normal_tex"), 1);
				glUniform1i(geometryInject_layered("i_RSMsize"), RSMSIZE);
				glUniform1f(geometryInject_layered("f_cellSize"), cellSize);
				glUniform1f(geometryInject_layered("f_tanFovXHalf"), f_tanFovXHalf);
				glUniform1f(geometryInject_layered("f_tanFovYHalf"), f_tanFovYHalf);
				glUniform1f(geometryInject_layered("f_texelAreaModifier"), f_texelAreaModifier);
				glUniform3f(geometryInject_layered("v_gridDim"), volumeDimensions.x, volumeDimensions.y, volumeDimensions.z);
				glUniform3f(geometryInject_layered("v_min"), vMin.x, vMin.y, vMin.z);
				glUniform3f(geometryInject_layered("v_lightPos"), lightPosition.x, lightPosition.y, lightPosition.z);
				glUniformMatrix4fv(geometryInject_layered("m_lightView"), 1, GL_FALSE, glm::value_ptr(v_light));
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, texManager["rsm_world_space_coords_tex"]);
				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, texManager["rsm_normal_tex"]);
				glBindVertexArray(VPLsVAO);
				glDrawArrays(GL_POINTS, 0, VPL_COUNT);
				glBindVertexArray(0);
				geometryInject_layered.UnUse();
				glDisable(GL_BLEND);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
			}
			else {
				if (b_compileAndUseAtomicShaders) {
					geometryInject.Use();
					glUniform1i(geometryInject("GeometryVolume"), 0);
					glUniform1i(geometryInject("rsm_world_space_coords_tex"), 0);
					glUniform1i(geometryInject("rsm_normal_tex"), 1);
					glUniform1i(geometryInject("i_RSMsize"), RSMSIZE);
					glUniform1f(geometryInject("f_cellSize"), cellSize);
					glUniform1f(geometryInject("f_tanFovXHalf"), f_tanFovXHalf);
					glUniform1f(geometryInject("f_tanFovYHalf"), f_tanFovYHalf);
					glUniform1f(geometryInject("f_texelAreaModifier"), f_texelAreaModifier);
					glUniform3f(geometryInject("v_gridDim"), volumeDimensions.x, volumeDimensions.y, volumeDimensions.z);
					glUniform3f(geometryInject("v_min"), vMin.x, vMin.y, vMin.z);
					glUniform3f(geometryInject("v_lightPos"), lightPosition.x, lightPosition.y, lightPosition.z);
					glUniformMatrix4fv(geometryInject("m_lightView"), 1, GL_FALSE, glm::value_ptr(v_light));
					//glUniformMatrix4fv(geometryInject("m_lightView"), 1, GL_FALSE, glm::value_ptr(v));
					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, texManager["rsm_world_space_coords_tex"]);
					glActiveTexture(GL_TEXTURE1);
					glBindTexture(GL_TEXTURE_2D, texManager["rsm_normal_tex"]);
					glBindImageTexture(0, geometryInjectCascadeTextures[i], 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
					glBindVertexArray(VPLsVAO);
					glDrawArrays(GL_POINTS, 0, VPL_COUNT);
					glBindVertexArray(0);
					geometryInject.UnUse();
				}
			}
		}
		inject.stop();

		////////////////////////////////////////////////////
		// LIGHT PROPAGATION
		////////////////////////////////////////////////////
		propagation.start();
		if (b_useLayeredFill) {
			for (int l = 0; l < end; l++) {
				propagate_layered(l);
			}
		}
		else {
			if (b_compileAndUseAtomicShaders) {
				for (int l = 0; l < end; l++) {
					propagate(l);
				}
			}
		}
		propagation.stop();
	}

	if (b_profileMode && !b_firstFrame) {
		RSMTimes << RSM.getElapsedTime() << std::endl;
		injectTimes << inject.getElapsedTime() << std::endl;
		PropagationTimes << propagation.getElapsedTime() << std::endl;
	}

	finalLighting.start();
	////////////////////////////////////////////////////
	// RENDER SCENE TO TEXTURE
	////////////////////////////////////////////////////
	glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	//glCullFace(GL_BACK);
	glViewport(0, 0, WIDTH, HEIGHT);
	glBindFramebuffer(GL_FRAMEBUFFER, fboManager->getFboId());
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	basicShader.Use();
	glUniform1i(basicShader("RAccumulatorLPV_l0"), 3);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_3D, accumulatorCascadeTextures[0].red);
	glUniform1i(basicShader("GAccumulatorLPV_l0"), 4);
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_3D, accumulatorCascadeTextures[0].green);
	glUniform1i(basicShader("BAccumulatorLPV_l0"), 5);
	glActiveTexture(GL_TEXTURE5);
	glBindTexture(GL_TEXTURE_3D, accumulatorCascadeTextures[0].blue);

	glUniform1i(basicShader("RAccumulatorLPV_l1"), 6);
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_3D, accumulatorCascadeTextures[1].red);
	glUniform1i(basicShader("GAccumulatorLPV_l1"), 7);
	glActiveTexture(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_3D, accumulatorCascadeTextures[1].green);
	glUniform1i(basicShader("BAccumulatorLPV_l1"), 8);
	glActiveTexture(GL_TEXTURE8);
	glBindTexture(GL_TEXTURE_3D, accumulatorCascadeTextures[1].blue);

	glUniform1i(basicShader("RAccumulatorLPV_l2"), 9);
	glActiveTexture(GL_TEXTURE9);
	glBindTexture(GL_TEXTURE_3D, accumulatorCascadeTextures[2].red);
	glUniform1i(basicShader("GAccumulatorLPV_l2"), 10);
	glActiveTexture(GL_TEXTURE10);
	glBindTexture(GL_TEXTURE_3D, accumulatorCascadeTextures[2].green);
	glUniform1i(basicShader("BAccumulatorLPV_l2"), 11);
	glActiveTexture(GL_TEXTURE11);
	glBindTexture(GL_TEXTURE_3D, accumulatorCascadeTextures[2].blue);

	glUniform1f(basicShader("f_indirectAttenuation"), f_indirectAttenuation);// f_indirectAttenuation
	glUniform1i(basicShader("b_enableGI"), b_enableGI);
	glUniform1i(basicShader("b_enableCascades"), b_enableCascades);
	glUniform1i(basicShader("b_lightIntesityOnly"), b_lightIntesityOnly);
	glUniform1i(basicShader("b_interpolateBorders"), b_interpolateBorders);
	glUniform3f(basicShader("v_gridDim"), volumeDimensions.x, volumeDimensions.y, volumeDimensions.z);

	v_allGridMins[0] = levels[0].getMin();
	v_allGridMins[1] = levels[1].getMin();
	v_allGridMins[2] = levels[2].getMin();

	v_allCellSizes = glm::vec3(levels[0].getCellSize(), levels[1].getCellSize(), levels[2].getCellSize());

	glUniform3fv(basicShader("v_allGridMins"), 3, glm::value_ptr(v_allGridMins[0]));
	glUniform3fv(basicShader("v_allCellSizes"), 1, glm::value_ptr(v_allCellSizes));
	//glBindImageTexture(0, texManager["AccumulatorLPV"], 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
	//glUniform1i(basicShader("tex"), 0); //Texture unit 0 is for base images.
	glUniform1i(basicShader("depthTexture"), 1); //Texture unit 1 is for shadow maps.
	glUniformMatrix4fv(basicShader("mvp"), 1, GL_FALSE, glm::value_ptr(mvp));
	glUniformMatrix4fv(basicShader("mv"), 1, GL_FALSE, glm::value_ptr(mv));
	glUniformMatrix4fv(basicShader("v"), 1, GL_FALSE, glm::value_ptr(v));
	glUniformMatrix4fv(basicShader("shadowMatrix"), 1, GL_FALSE, glm::value_ptr(biasMatrix*mvp_light));
	glUniformMatrix3fv(basicShader("mn"), 1, GL_FALSE, glm::value_ptr(mn));
	glUniform3f(basicShader("vLightPos"), lightPosition.x, lightPosition.y, lightPosition.z);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, texManager["rsm_depth_tex"]);
	mesh->render();
	glBindTexture(GL_TEXTURE_2D, 0);
	basicShader.UnUse();
	if (b_showGrids) {
		dd->setVPMatrix(mvp);
		dd->updateVBO(&(CBoundingBox::calculatePointDimensions(levels[0].getMin(), levels[0].getMax())));
		dd->draw();
		if (CASCADES >= 3) {
			dd_l1->setVPMatrix(mvp);
			dd_l1->updateVBO(&(CBoundingBox::calculatePointDimensions(levels[1].getMin(), levels[1].getMax())));
			dd_l1->draw();
			dd_l2->setVPMatrix(mvp);
			dd_l2->updateVBO(&(CBoundingBox::calculatePointDimensions(levels[2].getMin(), levels[2].getMax())));
			dd_l2->draw();
		}
	}
	////////////////////////////////////////////////////
	// VPL DEBUG DRAW
	////////////////////////////////////////////////////
#ifdef VPL_DEBUG
	glEnable(GL_PROGRAM_POINT_SIZE);
	glPointSize(2.5f);
	VPLsDebug.Use();
	glUniformMatrix4fv(VPLsDebug("mvp"), 1, GL_FALSE, glm::value_ptr(mvp));
	glUniform1i(VPLsDebug("i_RSMsize"), RSMSIZE);
	glUniform1i(VPLsDebug("rsm_world_space_coords_tex"), 0);
	glUniform1i(VPLsDebug("rsm_normal_tex"), 1);
	glUniform1i(VPLsDebug("b_useNormalOffset"), b_useNormalOffset);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texManager["rsm_world_space_coords_tex"]);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, texManager["rsm_normal_tex"]);
	glBindVertexArray(VPLsVAO);
	glDrawArrays(GL_POINTS, 0, VPL_COUNT);
	glBindVertexArray(0);
	VPLsDebug.UnUse();
	glPointSize(1.0f);
	glDisable(GL_PROGRAM_POINT_SIZE);
#endif

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	finalLighting.stop();
	//std::cout << finalLighting.getElapsedTime() << endl;

	////////////////////////////////////////////////////
	// FINAL COMPOSITION
	////////////////////////////////////////////////////
	//Draw quad on screen
	glViewport(0, 0, WIDTH, HEIGHT);
	glDisable(GL_DEPTH_TEST);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	ctv2->setTexture(texManager["render_tex"]);
	ctv2->draw();
	
	b_firstFrame = false;
	
}

/*
Helper function for displaying content of texture (2d only)
*/
void DisplayTexture(CTextureViewer * ctv) {

	//glEnable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	ctv->draw();
}

/*
Function which cleans up the mess before closing the window
*/
void Finalize(void) {

	delete ctv;
	delete ctv2;

	delete controlCamera;
	delete mesh;
	delete fboManager;
	delete RSMFboManager;
	delete light;
	delete dd;
	if (dd_l1 != NULL)
		delete dd_l1;
	if (dd_l2 != NULL)
		delete dd_l2;
	delete gBuffer;
	if (keyFrames.is_open()) {
		keyFrames.close();
	}

	if (injectTimes.is_open()) {
		injectTimes.close();
	}
	if (PropagationTimes.is_open()) {
		PropagationTimes.close();
	}
	if (RSMTimes.is_open()) {
		RSMTimes.close();
	}
}

void Reshape(int width, int height){
	glViewport(0, 0, width, height);
	aspect = float(height) / float(width);
}

void printVector(glm::vec3 v) {
	std::cout << v.x << ", " << v.y << ", " << v.z << std::endl;
}

/*
Update movable grid
*/
void updateGrid() {
	if (b_movableLPV) {
		glm::vec3 pos, dir;
		if (b_animation) {
			pos = tmp->getAnimationCameraPosition();
			dir = tmp->getAnimationCameraDirection();
		}
		else {
			pos = controlCamera->getPosition();
			dir = controlCamera->getDirection();
		}
		levels[0].translateGrid(pos, dir);
		levels[1].translateGrid(pos, dir);
		levels[2].translateGrid(pos, dir);
	}
	//vMin = levels[level].getMin();
	//printVector(vMin);
}

/*
Sets window's title
*/
void setTitle(SDL_Window * w) {
	string casc = (b_enableCascades) ? "yes" : "no";
	string layered = (b_useLayeredFill) ? "yes" : "no";
	string occ = (b_useOcclusion) ? "yes" : "no";
	string grid = (b_movableLPV) ? "yes" : "no";
	string title = "CLPV Grid: " + std::to_string(MAX_GRID_SIZE) + " props: " + std::to_string(PROPAGATION_STEPS) + " layered: " + layered + " cascades: " + casc + " occlusion: " + occ + " movable grid: " + grid;
	SDL_SetWindowTitle(w, title.c_str());

}

/*
Handles parameters
*/
void processParams(int argc, char **argv) {
	std::string arg, sample;
	for (int i = 1; i<argc; i++){
		if (argc > 1) {
			arg = "";
			arg.append(argv[i]);

			sample = "-animation";
			unsigned found = arg.find(sample);
			if (found != std::string::npos) {
				b_animation = true;
				b_profileMode = true;
			}

			sample = "-atomic";
			found = arg.find(sample);
			if (found != std::string::npos) {
				b_useLayeredFill = false;
			}

			sample = "-disableCascades";
			found = arg.find(sample);
			if (found != std::string::npos) {
				b_enableCascades = false;
				b_movableLPV = false;
			}
		}
	}
}

/*
Main function
*/
int main(int argc, char **argv) {
	//ilutRenderer(ILUT_OPENGL);
	ilInit();
	iluInit();
	//ilutInit();

	processParams(argc, argv);

	SDL_Window *mainwindow; 
	SDL_GLContext maincontext; 

	if (SDL_Init(SDL_INIT_VIDEO) < 0) { 
		std::cout << "Unable to initialize SDL";
		return 1;
	}

	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	//SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

	mainwindow = SDL_CreateWindow("Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		WIDTH, HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
	if (!mainwindow){ /* Die if creation failed */
		std::cout << "SDL Error: " << SDL_GetError() << std::endl;
		SDL_Quit();
		return 1;
	}

	setTitle(mainwindow);

	maincontext = SDL_GL_CreateContext(mainwindow);
	//SDL_GL_MakeCurrent(mainwindow, maincontext);

	GLenum rev;
	glewExperimental = GL_TRUE;
	rev = glewInit();

	if (GLEW_OK != rev){
		std::cout << "Error: " << glewGetErrorString(rev) << std::endl;
		exit(1);
	}
	else {
		std::cout << "GLEW Init: Success!" << std::endl;
		std::cout << "Status: Using GLEW " << glewGetString(GLEW_VERSION) << std::endl;
	}



	/* This makes our buffer swap syncronized with the monitor's vertical refresh */
	//SDL_GL_SetSwapInterval(1);

	bool quit = false;
	Initialize(mainwindow);
	Reshape(WIDTH, HEIGHT);

	SDL_Event event;
	const Uint8 * keys;
	Uint32 old_time, current_time;
	current_time = SDL_GetTicks();
	while (!quit){
		while (SDL_PollEvent(&event)){
			if (event.type == SDL_QUIT){
				//std::cout << "yes" << std::endl;
				quit = true;
			}
			if (event.type == SDL_MOUSEMOTION) {
				if (event.motion.state & SDL_BUTTON_LMASK)
				{
					controlCamera->moved = true;
					controlCamera->computeMatricesFromInputs();
					controlCamera->moved = false;
					updateGrid();
				}
			}

			if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
				if (event.key.keysym.sym == SDLK_p)
					b_useMultiStepPropagation  = !b_useMultiStepPropagation ;
				if (event.key.keysym.sym == SDLK_o) {
					b_useOcclusion = !b_useOcclusion;
					setTitle(mainwindow);
				}
				if (event.key.keysym.sym == SDLK_c) {
					f_indirectAttenuation += 0.1;
					//cout << f_indirectAttenuation << endl;
				}
				if (event.key.keysym.sym == SDLK_v) {
					if (f_indirectAttenuation >= 0.2) {
						f_indirectAttenuation -= 0.1;
						//cout << f_indirectAttenuation << endl;
					}
				}

				if (event.key.keysym.sym == SDLK_m) {
					if (b_enableCascades) {
						b_movableLPV = !b_movableLPV;
						setTitle(mainwindow);
					}
				}
				if (event.key.keysym.sym == SDLK_n) {
					b_showGrids = !b_showGrids;
				}

				if (event.key.keysym.sym == SDLK_y) {
					if (PROPAGATION_STEPS > 2) {
						--PROPAGATION_STEPS;
						setTitle(mainwindow);
					}
				}
				if (event.key.keysym.sym == SDLK_x) {
					if (PROPAGATION_STEPS < MAX_PROPAGATION_STEPS) {
						++PROPAGATION_STEPS;
						setTitle(mainwindow);
					}
				}
				if (event.key.keysym.sym == SDLK_g) {
					b_enableGI = !b_enableGI;
				}
				if (event.key.keysym.sym == SDLK_h) {
					b_enableCascades = !b_enableCascades;
					if (b_enableCascades) {
						b_movableLPV = true;
					}
					else {
						b_movableLPV = false;
					}
					setTitle(mainwindow);
				}
				if (event.key.keysym.sym == SDLK_t) {
					//keyFrames
					if (b_canWriteToFile) {
						glm::vec3 camPos = controlCamera->getPosition();
						glm::vec3 camDirection = controlCamera->getDirection();
						glm::vec3 camUp = controlCamera->getUp();
						glm::vec3 camRight = controlCamera->getRight();
						float camFov =  45.0f;
						float aspec = (float)WIDTH / (float)HEIGHT;
						keyFrames << "p " << camPos.x << " " << camPos.y << " " << camPos.z << " ";
						keyFrames << "d " << camDirection.x << " " << camDirection.y << " " << camDirection.z << " ";
						keyFrames << "u " << camUp.x << " " << camUp.y << " " << camUp.z << " ";
						keyFrames << "r " << camRight.x << " " << camRight.y << " " << camRight.z << " ";
						keyFrames << "f " << camFov << " a " << aspec << std::endl;;
						break;
					}
				}
				if (event.key.keysym.sym == SDLK_r){
					//if (!b_animation) {
					//	controlCamera->initControlCamera(initialCameraPos, mainwindow, initialCamHorAngle, initialCamVerAngle, WIDTH, HEIGHT, 1.0, 1000.0);
					//	controlCamera->moved = true;
					//	controlCamera->computeMatricesFromInputs();
					//	controlCamera->moved = false;
					//}
					//else {
					//	b_animation = true;
					//}
					b_animation = !b_animation;
					currIndex = 0;
					inc = 1;
				}
				if (event.key.keysym.sym == SDLK_l) {
					b_useLayeredFill = !b_useLayeredFill;
					setTitle(mainwindow);
				}
				if (event.key.keysym.sym == SDLK_i) {
					b_lightIntesityOnly = !b_lightIntesityOnly;
				}
				if (event.key.keysym.sym == SDLK_k) {
					b_interpolateBorders = !b_interpolateBorders;
				}
				if (event.key.keysym.sym == SDLK_KP_MINUS) {
					light->setVerAngle(light->getVerAngle() - 0.01f);
					//cout << light->getVerAngle() << endl;
				}
				if (event.key.keysym.sym == SDLK_KP_PLUS) {
					light->setVerAngle(light->getVerAngle() + 0.01f);
					//cout << light->getVerAngle() << endl;
				}
				if (event.key.keysym.sym == SDLK_ESCAPE) {
					//std::cout << "ESC\n";
					kill();
				}
			}
		}
		old_time = current_time;
		current_time = SDL_GetTicks();
		ftime = (current_time - old_time) / 1000.0f;
		float a = (current_time - old_time) / 17.0;
		//cout << a << " " << static_cast<int>(a) << endl;
		if (!b_firstFrame) {
			inc = static_cast<int>(a + 0.5);
			if (inc == 0) inc = 1;
		}

		keys = SDL_GetKeyboardState(NULL);
		if (keys[SDL_SCANCODE_W]) {
			controlCamera->setPosition(controlCamera->getPosition() + (controlCamera->getDirection() * movementSpeed * ftime));
			updateGrid();
		}
		else if (keys[SDL_SCANCODE_S]) {
			controlCamera->setPosition(controlCamera->getPosition() - (controlCamera->getDirection() * movementSpeed * ftime));
			updateGrid();
		}
		else if (keys[SDL_SCANCODE_A]) {
			controlCamera->setPosition(controlCamera->getPosition() - (controlCamera->getRight() * movementSpeed * ftime));
			updateGrid();
		}
		else if (keys[SDL_SCANCODE_D]) {
			controlCamera->setPosition(controlCamera->getPosition() + (controlCamera->getRight() * movementSpeed * ftime));
			updateGrid();
		}
		else if (keys[SDL_SCANCODE_KP_8]) {
			light->setPosition(light->getPosition() + (glm::vec3(0, 1, 0)* movementSpeed * 2.0f * ftime));
		}
		else if (keys[SDL_SCANCODE_KP_2]) {
			light->setPosition(light->getPosition() - (glm::vec3(0, 1, 0)* movementSpeed * 2.0f * ftime));
		}
		else if (keys[SDL_SCANCODE_KP_4]) {
			light->setPosition(light->getPosition() - (glm::vec3(1, 0, 0)* movementSpeed * 2.f *ftime));
		}
		else if (keys[SDL_SCANCODE_KP_6]) {
			light->setPosition(light->getPosition() + (glm::vec3(1, 0, 0)* movementSpeed * 2.f* ftime));
		}
		//else if (keys[SDL_SCANCODE_KP_9]) {
		//	light->setPosition(light->getPosition() - (glm::vec3(0, 0, 1)* movementSpeed * ftime));
		//}
		//else if (keys[SDL_SCANCODE_KP_3]) {
		//	light->setPosition(light->getPosition() + (glm::vec3(0, 0, 1)* movementSpeed * ftime));
		//}

		else if (keys[SDL_SCANCODE_KP_DIVIDE]) {
			light->setHorAngle(light->getHorAngle() + 0.01f);
		}
		else if (keys[SDL_SCANCODE_KP_MULTIPLY]) {
			light->setHorAngle(light->getHorAngle() - 0.01f);
		}

		SDL_GL_MakeCurrent(mainwindow, maincontext);
		Display();
		SDL_GL_SwapWindow(mainwindow);

	}
	Finalize();

	/* Delete our opengl context, destroy our window, and shutdown SDL */
	SDL_GL_DeleteContext(maincontext);
	SDL_DestroyWindow(mainwindow);

	SDL_Quit();

	return 0;
}

