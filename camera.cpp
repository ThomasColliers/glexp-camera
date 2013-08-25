#include <iostream>
#include <vector>
#include <string>
#include <stdlib.h>
#include <boost/filesystem.hpp>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <vector>

#include "ShaderManager.h"
#include "TriangleBatch.h"
#include "Batch.h"
#include "GeometryFactory.h"
#include "MatrixStack.h"
#include "Frustum.h"
#include "TransformPipeline.h"
#include "Math3D.h"
#include "UniformManager.h"
#include "TextureManager.h"
#include "ModelLoader.h"

using namespace gliby;
using namespace Math3D;
using namespace std;

// window
GLFWwindow* window;
int window_w, window_h;
// shader stuff
ShaderManager* shaderManager;
GLuint opaqueShader;
GLuint transparentShader;
// transformation stuff
Frame cameraFrame;
Frustum viewFrustum;
TransformPipeline transformPipeline;
MatrixStack modelViewMatrix;
MatrixStack projectionMatrix;
// objects
vector<Model*>* scene;
// movement coordinates
float cameraHeight = 605.0f;
Vector3f coordinates[] = {
/*    {-1181.0f,cameraHeight,32.84f},
    {-1159.0f,cameraHeight,-394.22f},
    {1086.0f,cameraHeight,-407.35f},
    {1073.0f,cameraHeight,446.36f},
    {-1159.0f,cameraHeight,406.95f},*/
    {843.63989f,107.76762f,2.26488f},
    {152.38356f,53.5464f,6.84681f},
    {-924.83679f,86.78181f,26.37549f},
    {-970.13684f,172.34787f,-97.64067f},
    {-326.90875f,530.20837f,156.22194f},
    {98.68172f,554.2002f,164.11856f},
    {956.23224f,209.57523f,39.62775f},
};
float* segmentLengths;
float totalLength; 
Vector3f previousPosition;
bool cameraMoving;
// texture
TextureManager* textureManager;
// uniform locations
UniformManager* opaqueUniformManager;
UniformManager* transparentUniformManager;

// TODO: Objects without a texture?

// TODO: Shadows
// TODO: Bump/normal mapping
// TODO: Specular mapping

// TODO: Add free camera mode
// TODO: Try and give the complete path a fixed speed by calculating the distance in between points
// TODO: Check other camera usage modes in class I found online

// TODO: z-fighting on the lionhead?

void setupContext(void){
    // general state
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DITHER);
    //glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    //glEnable(GL_DEPTH_CLAMP);

    // transform pipeline
    transformPipeline.setMatrixStacks(modelViewMatrix,projectionMatrix);
    viewFrustum.setPerspective(35.0f, float(window_w)/float(window_h), 1.0f, 5000.0f);
    projectionMatrix.loadMatrix(viewFrustum.getProjectionMatrix());
    modelViewMatrix.loadIdentity();

    // shader setup
    const char* searchPath[] = {"./shaders/","/home/ego/projects/personal/gliby/shaders/"};
    shaderManager = new ShaderManager(sizeof(searchPath)/sizeof(char*),searchPath);
    ShaderAttribute attrs[] = {{0,"vVertex"},{2,"vNormal"},{3,"vTexCoord"}};
    // the opaque shader
    opaqueShader = shaderManager->buildShaderPair("diffuse_specular.vp","diffuse_specular.fp",sizeof(attrs)/sizeof(ShaderAttribute),attrs);
    const char* opaqueUniforms[] = {"mvpMatrix","mvMatrix","normalMatrix","lightPosition","ambientColor","diffuseColor","textureUnit","specularColor","shinyness"};
    opaqueUniformManager = new UniformManager(opaqueShader,sizeof(opaqueUniforms)/sizeof(char*),opaqueUniforms);
    // the transparent shader
    transparentShader = shaderManager->buildShaderPair("diffuse_specular_transparent.vp","diffuse_specular_transparent.fp",sizeof(attrs)/sizeof(ShaderAttribute),attrs);
    const char* transparentUniforms[] = {"mvpMatrix","mvMatrix","normalMatrix","lightPosition","ambientColor","diffuseColor","diffuseTextureUnit","transparencyTextureUnit","specularColor","shinyness"};
    transparentUniformManager = new UniformManager(transparentShader,sizeof(transparentUniforms)/sizeof(char*),transparentUniforms);

    // setup texture loader
    textureManager = new TextureManager();

    // setup geometry
    ModelLoader modelLoader(textureManager);
    scene = modelLoader.loadAll("models/sponza.obj");

    // setup textures
    /*const char* textures[] = {"textures/spnza_bricks_a_diff.tga"};
    textureManager.loadTextures(sizeof(textures)/sizeof(char*),textures,GL_TEXTURE_2D,GL_TEXTURE0);*/

    cameraMoving = true;

    // calculate bezier lengths
    int numsegments = sizeof(coordinates) / sizeof(Vector3f);
    segmentLengths = new float[numsegments];
    totalLength = 0;
    for(int i = 0; i < numsegments; i++){
        int nextindex = i + 1;
        if(nextindex > numsegments - 1) nextindex = 0;
        Vector3f a = {coordinates[i][0],coordinates[i][1],coordinates[i][2]};
        Vector3f b = {coordinates[nextindex][0],coordinates[nextindex][1],coordinates[nextindex][2]};
        segmentLengths[i] = getDistance(a,b);
        totalLength += segmentLengths[i];
    }
}

void determinePositionOnSpline(double t, Vector3f output){
    int numsegments = sizeof(coordinates) / sizeof(Vector3f);
    // find out which segment we are on the spline
    int segment = numsegments * t;
    // calculate the time on this segment
    float perseg = 1.0f/numsegments;
    double lt = (t-(segment*perseg))/perseg;
    // determine the array indexes, loop them around (shouldn't I be using mod for this too?)
    int indexes[] = {segment-1,segment,segment+1,segment+2};
    for(int i = 0; i < 4; i++){
        if(indexes[i] < 0){
            indexes[i] = numsegments+indexes[i];
        }else if(indexes[i] > numsegments-1){
            indexes[i] = abs(numsegments-indexes[i]);
        }
    }
    catmullRom(output, coordinates[indexes[0]], coordinates[indexes[1]], coordinates[indexes[2]], coordinates[indexes[3]], lt);
}

void render(void){
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // setup camera
    if(cameraMoving){
        double t = fmod(glfwGetTime(),10.0d)/10.0d;
        Vector3f position;
        determinePositionOnSpline(t, position);
        cameraFrame.setOrigin(position[0],position[1],position[2]);
        // look at a position in the future
        double lookAhead = 0.5d;
        t = fmod(glfwGetTime()+lookAhead,10.0d)/10.0d;
        Vector3f orientation;
        determinePositionOnSpline(t, orientation);
        cameraFrame.lookAt(orientation[0],orientation[1],orientation[2]);
    }

    // setup matrix
    Matrix44f mCamera;
    cameraFrame.getCameraMatrix(mCamera);
    modelViewMatrix.pushMatrix();
    modelViewMatrix.multMatrix(mCamera);

    glDisable(GL_BLEND);
    // render opaque objects
    glUseProgram(opaqueShader);
    glUniformMatrix4fv(opaqueUniformManager->get("mvpMatrix"),1,GL_FALSE,transformPipeline.getModelViewProjectionMatrix());
    glUniformMatrix4fv(opaqueUniformManager->get("mvMatrix"),1,GL_FALSE,transformPipeline.getModelViewMatrix());
    glUniformMatrix3fv(opaqueUniformManager->get("normalMatrix"),1,GL_FALSE,transformPipeline.getNormalMatrix());
    GLfloat lightPosition[] = {2.0f,2.0f,2.0f};
    glUniform3fv(opaqueUniformManager->get("lightPosition"),1,lightPosition);
    GLfloat ambientColor[] = {0.2f, 0.2f, 0.2f, 1.0f};
    glUniform4fv(opaqueUniformManager->get("ambientColor"),1,ambientColor);
    GLfloat diffuseColor[] = {0.7f, 0.7f, 0.7f, 1.0f};
    glUniform4fv(opaqueUniformManager->get("diffuseColor"),1,diffuseColor);
    GLfloat specularColor[] = {0.2f, 0.2f, 0.2f, 1.0f};
    glUniform4fv(opaqueUniformManager->get("specularColor"),1,specularColor);
    glUniform1i(opaqueUniformManager->get("textureUnit"),0);
    for(vector<Model*>::iterator it = scene->begin(); it != scene->end(); ++it) {
        Model* m = *it;
        Material* mat = m->getMaterial();
        if(mat->getTextureOpacity()) continue;
        const char* tex = mat->getTextureDiffuse();
        glActiveTexture(GL_TEXTURE0);
        if(tex) glBindTexture(GL_TEXTURE_2D, textureManager->get(tex));
        glUniform1f(opaqueUniformManager->get("shinyness"),mat->getShininess());
        m->draw();
    }

    glEnable(GL_BLEND);
    // render transparent objects
    glUseProgram(transparentShader);
    glUniformMatrix4fv(transparentUniformManager->get("mvpMatrix"),1,GL_FALSE,transformPipeline.getModelViewProjectionMatrix());
    glUniformMatrix4fv(transparentUniformManager->get("mvMatrix"),1,GL_FALSE,transformPipeline.getModelViewMatrix());
    glUniformMatrix3fv(transparentUniformManager->get("normalMatrix"),1,GL_FALSE,transformPipeline.getNormalMatrix());
    glUniform3fv(transparentUniformManager->get("lightPosition"),1,lightPosition);
    glUniform4fv(transparentUniformManager->get("ambientColor"),1,ambientColor);
    glUniform4fv(transparentUniformManager->get("diffuseColor"),1,diffuseColor);
    glUniform4fv(transparentUniformManager->get("specularColor"),1,specularColor);
    glUniform1i(transparentUniformManager->get("diffuseTextureUnit"),0);
    glUniform1i(transparentUniformManager->get("transparencyTextureUnit"),1);
    for(vector<Model*>::iterator it = scene->begin(); it != scene->end(); ++it) {
        Model* m = *it;
        Material* mat = m->getMaterial();
        if(!mat->getTextureOpacity()) continue;
        const char* tex = mat->getTextureDiffuse();
        glActiveTexture(GL_TEXTURE0);
        if(tex) glBindTexture(GL_TEXTURE_2D, textureManager->get(tex));
        glActiveTexture(GL_TEXTURE1);
        tex = mat->getTextureOpacity();
        if(tex) glBindTexture(GL_TEXTURE_2D, textureManager->get(tex));
        glUniform1f(transparentUniformManager->get("shinyness"),mat->getShininess());
        m->draw();
    }


    modelViewMatrix.popMatrix();
}

void keyCallback(GLFWwindow* window, int key, int scancode, int state, int mods){
    if(key == GLFW_KEY_ESCAPE && state == GLFW_RELEASE){
        glfwSetWindowShouldClose(window, true);
    }
    if(key == GLFW_KEY_SPACE && state == GLFW_RELEASE){
        cameraMoving = !cameraMoving;
    }
}

void resizeCallback(GLFWwindow* window, int width, int height){
    window_w = width;
    window_h = height;
    glViewport(0,0,window_w,window_h);
    viewFrustum.setPerspective(45.0f, float(window_w)/float(window_h),0.1f,5000.0f);
    projectionMatrix.loadMatrix(viewFrustum.getProjectionMatrix());
}

int main(int argc, char **argv){
    // force vsync on
    putenv((char*) "__GL_SYNC_TO_VBLANK=1");

    // init glfw
    if(!glfwInit()){
        std::cerr << "GLFW init failed" << std::endl;
        return -1;
    }

    // swap interval
    glfwSwapInterval(1);
    // set window open hints
    glfwWindowHint(GLFW_SAMPLES, 8);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // create glfw window
    window = glfwCreateWindow(800,600,"gltest",NULL,NULL);
    window_w = 800; window_h = 600;
    if(!window){
        std::cerr << "GLFW window creation failed" << std::endl;
        glfwTerminate();
        return -1;
    }
    // make the window's context current
    glfwMakeContextCurrent(window);

    // event handlers
    glfwSetKeyCallback(window,keyCallback);
    glfwSetWindowSizeCallback(window,resizeCallback);

    // init glew
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if(err != GLEW_OK){
        std::cerr << "Glew error: " << glewGetErrorString(err) << std::endl;
    }

    // setup context
    setupContext();

    // main loop
    while(!glfwWindowShouldClose(window)){
        render();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
