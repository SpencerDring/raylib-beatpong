/**********************************************************************************************
*
*   rlgl - raylib OpenGL abstraction layer
*
*   raylib now uses OpenGL 1.1 style functions (rlVertex) that are mapped to selected OpenGL version:
*       OpenGL 1.1  - Direct map rl* -> gl*
*       OpenGL 3.3  - Vertex data is stored in VAOs, call rlglDraw() to render
*       OpenGL ES 2 - Vertex data is stored in VBOs or VAOs (when available), call rlglDraw() to render
*
*   Copyright (c) 2014 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#include "rlgl.h"

#include <stdio.h>          // Standard input / output lib
#include <stdlib.h>         // Declares malloc() and free() for memory management, rand()
#include <string.h>         // Declares strcmp(), strlen(), strtok()

#ifndef RLGL_STANDALONE
    #include "raymath.h"    // Required for Vector3 and Matrix functions
#endif

#if defined(GRAPHICS_API_OPENGL_11)
    #ifdef __APPLE__                // OpenGL include for OSX
        #include <OpenGL/gl.h>
    #else
        #include <GL/gl.h>          // Basic OpenGL include
    #endif
#endif

#if defined(GRAPHICS_API_OPENGL_33)
    #ifdef __APPLE__                // OpenGL include for OSX
        #include <OpenGL/gl3.h>
    #else
        //#define GLEW_STATIC
        //#include <GL/glew.h>        // GLEW header, includes OpenGL headers
        #include "glad.h"         // glad header, includes OpenGL headers
    #endif
#endif

#if defined(GRAPHICS_API_OPENGL_ES2)
    #include <EGL/egl.h>
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>
#endif

#if defined(RLGL_STANDALONE)
    #include <stdarg.h>         // Used for functions with variable number of parameters (TraceLog())
#endif

//----------------------------------------------------------------------------------
// Defines and Macros
//----------------------------------------------------------------------------------
#define MATRIX_STACK_SIZE          16   // Matrix stack max size
#define MAX_DRAWS_BY_TEXTURE      256   // Draws are organized by texture changes
#define TEMP_VERTEX_BUFFER_SIZE  4096   // Temporal Vertex Buffer (required for vertex-transformations)
                                        // NOTE: Every vertex are 3 floats (12 bytes)

#ifndef GL_SHADING_LANGUAGE_VERSION
    #define GL_SHADING_LANGUAGE_VERSION         0x8B8C
#endif

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
    #define GL_COMPRESSED_RGB_S3TC_DXT1_EXT     0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
    #define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT    0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
    #define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT    0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
    #define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT    0x83F3
#endif
#ifndef GL_ETC1_RGB8_OES
    #define GL_ETC1_RGB8_OES                    0x8D64
#endif
#ifndef GL_COMPRESSED_RGB8_ETC2
    #define GL_COMPRESSED_RGB8_ETC2             0x9274
#endif
#ifndef GL_COMPRESSED_RGBA8_ETC2_EAC
    #define GL_COMPRESSED_RGBA8_ETC2_EAC        0x9278
#endif
#ifndef GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG
    #define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG  0x8C00
#endif
#ifndef GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG
    #define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG 0x8C02
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
    #define GL_COMPRESSED_RGBA_ASTC_4x4_KHR     0x93b0
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_8x8_KHR
    #define GL_COMPRESSED_RGBA_ASTC_8x8_KHR     0x93b7
#endif

#if defined(GRAPHICS_API_OPENGL_11)
    #define GL_UNSIGNED_SHORT_5_6_5     0x8363
    #define GL_UNSIGNED_SHORT_5_5_5_1   0x8034
    #define GL_UNSIGNED_SHORT_4_4_4_4   0x8033
#endif
//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------

// Vertex buffer (position + color arrays)
// NOTE: Used for lines and triangles VAOs
typedef struct {
    int vCounter;
    int cCounter;
    float *vertices;            // 3 components per vertex
    unsigned char *colors;      // 4 components per vertex
} VertexPositionColorBuffer;

// Vertex buffer (position + texcoords + color arrays)
// NOTE: Not used
typedef struct {
    int vCounter;
    int tcCounter;
    int cCounter;
    float *vertices;            // 3 components per vertex
    float *texcoords;           // 2 components per vertex
    unsigned char *colors;      // 4 components per vertex
} VertexPositionColorTextureBuffer;

// Vertex buffer (position + texcoords + normals arrays)
// NOTE: Not used
typedef struct {
    int vCounter;
    int tcCounter;
    int nCounter;
    float *vertices;            // 3 components per vertex
    float *texcoords;           // 2 components per vertex
    float *normals;             // 3 components per vertex
    //short *normals;           // NOTE: Less data load... but padding issues and normalizing required!
} VertexPositionTextureNormalBuffer;

// Vertex buffer (position + texcoords + colors + indices arrays)
// NOTE: Used for quads VAO
typedef struct {
    int vCounter;
    int tcCounter;
    int cCounter;
    float *vertices;            // 3 components per vertex
    float *texcoords;           // 2 components per vertex
    unsigned char *colors;      // 4 components per vertex
#if defined(GRAPHICS_API_OPENGL_11) || defined(GRAPHICS_API_OPENGL_33)
    unsigned int *indices;      // 6 indices per quad (could be int)
#elif defined(GRAPHICS_API_OPENGL_ES2)
    unsigned short *indices;    // 6 indices per quad (must be short)
                                // NOTE: 6*2 byte = 12 byte, not alignment problem!
#endif
} VertexPositionColorTextureIndexBuffer;

// Draw call type
// NOTE: Used to track required draw-calls, organized by texture
typedef struct {
    GLuint textureId;
    int vertexCount;
    // TODO: Store draw state -> blending mode, shader
} DrawCall;

#if defined(RLGL_STANDALONE)
typedef enum { INFO = 0, ERROR, WARNING, DEBUG, OTHER } TraceLogType;
#endif

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
static Matrix stack[MATRIX_STACK_SIZE];
static int stackCounter = 0;

static Matrix modelview;
static Matrix projection;
static Matrix *currentMatrix;
static int currentMatrixMode;

static DrawMode currentDrawMode;

static float currentDepth = -1.0f;

// Default vertex buffers for lines, triangles and quads
static VertexPositionColorBuffer lines;         // No texture support
static VertexPositionColorBuffer triangles;     // No texture support
static VertexPositionColorTextureIndexBuffer quads;

// Default vertex buffers VAOs (if supported)
static GLuint vaoLines, vaoTriangles, vaoQuads;

// Default vertex buffers VBOs
static GLuint linesBuffer[2];           // Lines buffers (position, color)
static GLuint trianglesBuffer[2];       // Triangles buffers (position, color)
static GLuint quadsBuffer[4];           // Quads buffers (position, texcoord, color, index)

// Default buffers draw calls
static DrawCall *draws;
static int drawsCounter;

// Temp vertex buffer to be used with rlTranslate, rlRotate, rlScale
static Vector3 *tempBuffer;
static int tempBufferCount = 0;
static bool useTempBuffer = false;

// Shader Programs
static Shader defaultShader;
static Shader currentShader;            // By default, defaultShader

// Flags for supported extensions
static bool vaoSupported = false;   // VAO support (OpenGL ES2 could not support VAO extension)

// Compressed textures support flags
static bool texCompETC1Supported = false;    // ETC1 texture compression support
static bool texCompETC2Supported = false;    // ETC2/EAC texture compression support
static bool texCompPVRTSupported = false;    // PVR texture compression support
static bool texCompASTCSupported = false;    // ASTC texture compression support
#endif

// Compressed textures support flags
static bool texCompDXTSupported = false;     // DDS texture compression support
static bool npotSupported = false;           // NPOT textures full support

#if defined(GRAPHICS_API_OPENGL_ES2)
// NOTE: VAO functionality is exposed through extensions (OES)
static PFNGLGENVERTEXARRAYSOESPROC glGenVertexArrays;
static PFNGLBINDVERTEXARRAYOESPROC glBindVertexArray;
static PFNGLDELETEVERTEXARRAYSOESPROC glDeleteVertexArrays;
//static PFNGLISVERTEXARRAYOESPROC glIsVertexArray;        // NOTE: Fails in WebGL, omitted
#endif

static int blendMode = 0;

// White texture useful for plain color polys (required by shader)
// NOTE: It's required in shapes and models modules!
unsigned int whiteTexture;

//----------------------------------------------------------------------------------
// Module specific Functions Declaration
//----------------------------------------------------------------------------------
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
static void LoadCompressedTexture(unsigned char *data, int width, int height, int mipmapCount, int compressedFormat);

static Shader LoadDefaultShader(void);
static void LoadDefaultShaderLocations(Shader *shader);
static void UnloadDefaultShader(void);

static void LoadDefaultBuffers(void);
static void UpdateDefaultBuffers(void);
static void UnloadDefaultBuffers(void);

static char *ReadTextFile(const char *fileName);
#endif

#if defined(GRAPHICS_API_OPENGL_11)
static int GenerateMipmaps(unsigned char *data, int baseWidth, int baseHeight);
static Color *GenNextMipmap(Color *srcData, int srcWidth, int srcHeight);
#endif

#if defined(RLGL_STANDALONE)
static void TraceLog(int msgType, const char *text, ...);
float *MatrixToFloat(Matrix mat);   // Converts Matrix to float array
#endif

//----------------------------------------------------------------------------------
// Module Functions Definition - Matrix operations
//----------------------------------------------------------------------------------

#if defined(GRAPHICS_API_OPENGL_11)

// Fallback to OpenGL 1.1 function calls
//---------------------------------------
void rlMatrixMode(int mode)
{
    switch (mode)
    {
        case RL_PROJECTION: glMatrixMode(GL_PROJECTION); break;
        case RL_MODELVIEW: glMatrixMode(GL_MODELVIEW); break;
        case RL_TEXTURE: glMatrixMode(GL_TEXTURE); break;
        default: break;
    }
}

void rlFrustum(double left, double right, double bottom, double top, double near, double far)
{
    glFrustum(left, right, bottom, top, near, far);
}

void rlOrtho(double left, double right, double bottom, double top, double near, double far)
{
    glOrtho(left, right, bottom, top, near, far);
}

void rlPushMatrix(void) { glPushMatrix(); }
void rlPopMatrix(void) { glPopMatrix(); }
void rlLoadIdentity(void) { glLoadIdentity(); }
void rlTranslatef(float x, float y, float z) { glTranslatef(x, y, z); }
void rlRotatef(float angleDeg, float x, float y, float z) { glRotatef(angleDeg, x, y, z); }
void rlScalef(float x, float y, float z) { glScalef(x, y, z); }
void rlMultMatrixf(float *mat) { glMultMatrixf(mat); }

#elif defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)

// Choose the current matrix to be transformed
void rlMatrixMode(int mode)
{
    if (mode == RL_PROJECTION) currentMatrix = &projection;
    else if (mode == RL_MODELVIEW) currentMatrix = &modelview;
    //else if (mode == RL_TEXTURE) // Not supported

    currentMatrixMode = mode;
}

// Push the current matrix to stack
void rlPushMatrix(void)
{
    if (stackCounter == MATRIX_STACK_SIZE - 1)
    {
        TraceLog(ERROR, "Stack Buffer Overflow (MAX %i Matrix)", MATRIX_STACK_SIZE);
    }

    stack[stackCounter] = *currentMatrix;
    rlLoadIdentity();
    stackCounter++;

    if (currentMatrixMode == RL_MODELVIEW) useTempBuffer = true;
}

// Pop lattest inserted matrix from stack
void rlPopMatrix(void)
{
    if (stackCounter > 0)
    {
        Matrix mat = stack[stackCounter - 1];
        *currentMatrix = mat;
        stackCounter--;
    }
}

// Reset current matrix to identity matrix
void rlLoadIdentity(void)
{
    *currentMatrix = MatrixIdentity();
}

// Multiply the current matrix by a translation matrix
void rlTranslatef(float x, float y, float z)
{
    Matrix matTranslation = MatrixTranslate(x, y, z);
    MatrixTranspose(&matTranslation);

    *currentMatrix = MatrixMultiply(*currentMatrix, matTranslation);
}

// Multiply the current matrix by a rotation matrix
void rlRotatef(float angleDeg, float x, float y, float z)
{
    Matrix matRotation = MatrixIdentity();

    Vector3 axis = (Vector3){ x, y, z };
    VectorNormalize(&axis);
    matRotation = MatrixRotate(axis, angleDeg*DEG2RAD);

    MatrixTranspose(&matRotation);

    *currentMatrix = MatrixMultiply(*currentMatrix, matRotation);
}

// Multiply the current matrix by a scaling matrix
void rlScalef(float x, float y, float z)
{
    Matrix matScale = MatrixScale(x, y, z);
    MatrixTranspose(&matScale);

    *currentMatrix = MatrixMultiply(*currentMatrix, matScale);
}

// Multiply the current matrix by another matrix
void rlMultMatrixf(float *m)
{
    // Matrix creation from array
    Matrix mat = { m[0], m[1], m[2], m[3],
                   m[4], m[5], m[6], m[7],
                   m[8], m[9], m[10], m[11],
                   m[12], m[13], m[14], m[15] };

    *currentMatrix = MatrixMultiply(*currentMatrix, mat);
}

// Multiply the current matrix by a perspective matrix generated by parameters
void rlFrustum(double left, double right, double bottom, double top, double near, double far)
{
    Matrix matPerps = MatrixFrustum(left, right, bottom, top, near, far);
    MatrixTranspose(&matPerps);

    *currentMatrix = MatrixMultiply(*currentMatrix, matPerps);
}

// Multiply the current matrix by an orthographic matrix generated by parameters
void rlOrtho(double left, double right, double bottom, double top, double near, double far)
{
    Matrix matOrtho = MatrixOrtho(left, right, bottom, top, near, far);
    MatrixTranspose(&matOrtho);

    *currentMatrix = MatrixMultiply(*currentMatrix, matOrtho);
}

#endif

//----------------------------------------------------------------------------------
// Module Functions Definition - Vertex level operations
//----------------------------------------------------------------------------------
#if defined(GRAPHICS_API_OPENGL_11)

// Fallback to OpenGL 1.1 function calls
//---------------------------------------
void rlBegin(int mode)
{
    switch (mode)
    {
        case RL_LINES: glBegin(GL_LINES); break;
        case RL_TRIANGLES: glBegin(GL_TRIANGLES); break;
        case RL_QUADS: glBegin(GL_QUADS); break;
        default: break;
    }
}

void rlEnd() { glEnd(); }
void rlVertex2i(int x, int y) { glVertex2i(x, y); }
void rlVertex2f(float x, float y) { glVertex2f(x, y); }
void rlVertex3f(float x, float y, float z) { glVertex3f(x, y, z); }
void rlTexCoord2f(float x, float y) { glTexCoord2f(x, y); }
void rlNormal3f(float x, float y, float z) { glNormal3f(x, y, z); }
void rlColor4ub(byte r, byte g, byte b, byte a) { glColor4ub(r, g, b, a); }
void rlColor3f(float x, float y, float z) { glColor3f(x, y, z); }
void rlColor4f(float x, float y, float z, float w) { glColor4f(x, y, z, w); }

#elif defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)

// Initialize drawing mode (how to organize vertex)
void rlBegin(int mode)
{
    // Draw mode can only be RL_LINES, RL_TRIANGLES and RL_QUADS
    currentDrawMode = mode;
}

// Finish vertex providing
void rlEnd(void)
{
    if (useTempBuffer)
    {
        // NOTE: In this case, *currentMatrix is already transposed because transposing has been applied
        // independently to translation-scale-rotation matrices -> t(M1 x M2) = t(M2) x t(M1)
        // This way, rlTranslatef(), rlRotatef()... behaviour is the same than OpenGL 1.1

        // Apply transformation matrix to all temp vertices
        for (int i = 0; i < tempBufferCount; i++) VectorTransform(&tempBuffer[i], *currentMatrix);

        // Deactivate tempBuffer usage to allow rlVertex3f do its job
        useTempBuffer = false;

        // Copy all transformed vertices to right VAO
        for (int i = 0; i < tempBufferCount; i++) rlVertex3f(tempBuffer[i].x, tempBuffer[i].y, tempBuffer[i].z);

        // Reset temp buffer
        tempBufferCount = 0;
    }

    // Make sure vertexCount is the same for vertices-texcoords-normals-colors
    // NOTE: In OpenGL 1.1, one glColor call can be made for all the subsequent glVertex calls.
    switch (currentDrawMode)
    {
        case RL_LINES:
        {
            if (lines.vCounter != lines.cCounter)
            {
                int addColors = lines.vCounter - lines.cCounter;

                for (int i = 0; i < addColors; i++)
                {
                    lines.colors[4*lines.cCounter] = lines.colors[4*lines.cCounter - 4];
                    lines.colors[4*lines.cCounter + 1] = lines.colors[4*lines.cCounter - 3];
                    lines.colors[4*lines.cCounter + 2] = lines.colors[4*lines.cCounter - 2];
                    lines.colors[4*lines.cCounter + 3] = lines.colors[4*lines.cCounter - 1];

                    lines.cCounter++;
                }
            }
        } break;
        case RL_TRIANGLES:
        {
            if (triangles.vCounter != triangles.cCounter)
            {
                int addColors = triangles.vCounter - triangles.cCounter;

                for (int i = 0; i < addColors; i++)
                {
                    triangles.colors[4*triangles.cCounter] = triangles.colors[4*triangles.cCounter - 4];
                    triangles.colors[4*triangles.cCounter + 1] = triangles.colors[4*triangles.cCounter - 3];
                    triangles.colors[4*triangles.cCounter + 2] = triangles.colors[4*triangles.cCounter - 2];
                    triangles.colors[4*triangles.cCounter + 3] = triangles.colors[4*triangles.cCounter - 1];

                    triangles.cCounter++;
                }
            }
        } break;
        case RL_QUADS:
        {
            // Make sure colors count match vertex count
            if (quads.vCounter != quads.cCounter)
            {
                int addColors = quads.vCounter - quads.cCounter;

                for (int i = 0; i < addColors; i++)
                {
                    quads.colors[4*quads.cCounter] = quads.colors[4*quads.cCounter - 4];
                    quads.colors[4*quads.cCounter + 1] = quads.colors[4*quads.cCounter - 3];
                    quads.colors[4*quads.cCounter + 2] = quads.colors[4*quads.cCounter - 2];
                    quads.colors[4*quads.cCounter + 3] = quads.colors[4*quads.cCounter - 1];

                    quads.cCounter++;
                }
            }

            // Make sure texcoords count match vertex count
            if (quads.vCounter != quads.tcCounter)
            {
                int addTexCoords = quads.vCounter - quads.tcCounter;

                for (int i = 0; i < addTexCoords; i++)
                {
                    quads.texcoords[2*quads.tcCounter] = 0.0f;
                    quads.texcoords[2*quads.tcCounter + 1] = 0.0f;

                    quads.tcCounter++;
                }
            }

            // TODO: Make sure normals count match vertex count... if normals support is added in a future... :P

        } break;
        default: break;
    }
    
    // NOTE: Depth increment is dependant on rlOrtho(): z-near and z-far values,
    // as well as depth buffer bit-depth (16bit or 24bit or 32bit)
    // Correct increment formula would be: depthInc = (zfar - znear)/pow(2, bits)
    currentDepth += (1.0f/20000.0f);
}

// Define one vertex (position)
void rlVertex3f(float x, float y, float z)
{
    if (useTempBuffer)
    {
        tempBuffer[tempBufferCount].x = x;
        tempBuffer[tempBufferCount].y = y;
        tempBuffer[tempBufferCount].z = z;
        tempBufferCount++;
    }
    else
    {
        switch (currentDrawMode)
        {
            case RL_LINES:
            {
                // Verify that MAX_LINES_BATCH limit not reached
                if (lines.vCounter / 2 < MAX_LINES_BATCH)
                {
                    lines.vertices[3*lines.vCounter] = x;
                    lines.vertices[3*lines.vCounter + 1] = y;
                    lines.vertices[3*lines.vCounter + 2] = z;

                    lines.vCounter++;
                }
                else TraceLog(ERROR, "MAX_LINES_BATCH overflow");

            } break;
            case RL_TRIANGLES:
            {
                // Verify that MAX_TRIANGLES_BATCH limit not reached
                if (triangles.vCounter / 3 < MAX_TRIANGLES_BATCH)
                {
                    triangles.vertices[3*triangles.vCounter] = x;
                    triangles.vertices[3*triangles.vCounter + 1] = y;
                    triangles.vertices[3*triangles.vCounter + 2] = z;

                    triangles.vCounter++;
                }
                else TraceLog(ERROR, "MAX_TRIANGLES_BATCH overflow");

            } break;
            case RL_QUADS:
            {
                // Verify that MAX_QUADS_BATCH limit not reached
                if (quads.vCounter / 4 < MAX_QUADS_BATCH)
                {
                    quads.vertices[3*quads.vCounter] = x;
                    quads.vertices[3*quads.vCounter + 1] = y;
                    quads.vertices[3*quads.vCounter + 2] = z;

                    quads.vCounter++;

                    draws[drawsCounter - 1].vertexCount++;
                }
                else TraceLog(ERROR, "MAX_QUADS_BATCH overflow");

            } break;
            default: break;
        }
    }
}

// Define one vertex (position)
void rlVertex2f(float x, float y)
{
    rlVertex3f(x, y, currentDepth);
}

// Define one vertex (position)
void rlVertex2i(int x, int y)
{
    rlVertex3f((float)x, (float)y, currentDepth);
}

// Define one vertex (texture coordinate)
// NOTE: Texture coordinates are limited to QUADS only
void rlTexCoord2f(float x, float y)
{
    if (currentDrawMode == RL_QUADS)
    {
        quads.texcoords[2*quads.tcCounter] = x;
        quads.texcoords[2*quads.tcCounter + 1] = y;

        quads.tcCounter++;
    }
}

// Define one vertex (normal)
// NOTE: Normals limited to TRIANGLES only ?
void rlNormal3f(float x, float y, float z)
{
    // TODO: Normals usage...
}

// Define one vertex (color)
void rlColor4ub(byte x, byte y, byte z, byte w)
{
    switch (currentDrawMode)
    {
        case RL_LINES:
        {
            lines.colors[4*lines.cCounter] = x;
            lines.colors[4*lines.cCounter + 1] = y;
            lines.colors[4*lines.cCounter + 2] = z;
            lines.colors[4*lines.cCounter + 3] = w;

            lines.cCounter++;

        } break;
        case RL_TRIANGLES:
        {
            triangles.colors[4*triangles.cCounter] = x;
            triangles.colors[4*triangles.cCounter + 1] = y;
            triangles.colors[4*triangles.cCounter + 2] = z;
            triangles.colors[4*triangles.cCounter + 3] = w;

            triangles.cCounter++;

        } break;
        case RL_QUADS:
        {
            quads.colors[4*quads.cCounter] = x;
            quads.colors[4*quads.cCounter + 1] = y;
            quads.colors[4*quads.cCounter + 2] = z;
            quads.colors[4*quads.cCounter + 3] = w;

            quads.cCounter++;

        } break;
        default: break;
    }
}

// Define one vertex (color)
void rlColor4f(float r, float g, float b, float a)
{
    rlColor4ub((byte)(r*255), (byte)(g*255), (byte)(b*255), (byte)(a*255));
}

// Define one vertex (color)
void rlColor3f(float x, float y, float z)
{
    rlColor4ub((byte)(x*255), (byte)(y*255), (byte)(z*255), 255);
}

#endif

//----------------------------------------------------------------------------------
// Module Functions Definition - OpenGL equivalent functions (common to 1.1, 3.3+, ES2)
//----------------------------------------------------------------------------------

// Enable texture usage
void rlEnableTexture(unsigned int id)
{
#if defined(GRAPHICS_API_OPENGL_11)
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, id);
#endif

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    if (draws[drawsCounter - 1].textureId != id)
    {
        if (draws[drawsCounter - 1].vertexCount > 0) drawsCounter++;

        draws[drawsCounter - 1].textureId = id;
        draws[drawsCounter - 1].vertexCount = 0;
    }
#endif
}

// Disable texture usage
void rlDisableTexture(void)
{
#if defined(GRAPHICS_API_OPENGL_11)
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
#endif
}

void rlEnableRenderTexture(unsigned int id)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glBindFramebuffer(GL_FRAMEBUFFER, id);
#endif
}

void rlDisableRenderTexture(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
}

// Enable depth test
void rlEnableDepthTest(void)
{
    glEnable(GL_DEPTH_TEST);
}

// Disable depth test
void rlDisableDepthTest(void)
{
    glDisable(GL_DEPTH_TEST);
}

// Unload texture from GPU memory
void rlDeleteTextures(unsigned int id)
{
    glDeleteTextures(1, &id);
}

// Unload render texture from GPU memory
void rlDeleteRenderTextures(RenderTexture2D target)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glDeleteFramebuffers(1, &target.id);
    glDeleteTextures(1, &target.texture.id);
    glDeleteTextures(1, &target.depth.id);
#endif
}

// Unload shader from GPU memory
void rlDeleteShader(unsigned int id)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glDeleteProgram(id);
#endif
}

// Unload vertex data (VAO) from GPU memory
void rlDeleteVertexArrays(unsigned int id)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    if (vaoSupported) 
    {
        glDeleteVertexArrays(1, &id);
        TraceLog(INFO, "[VAO ID %i] Unloaded model data from VRAM (GPU)", id);
    }
#endif
}

// Unload vertex data (VBO) from GPU memory
void rlDeleteBuffers(unsigned int id)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glDeleteBuffers(1, &id);
    
    if (!vaoSupported) TraceLog(INFO, "[VBO ID %i] Unloaded model vertex data from VRAM (GPU)", id);
#endif
}

// Clear color buffer with color
void rlClearColor(byte r, byte g, byte b, byte a)
{
    // Color values clamp to 0.0f(0) and 1.0f(255)
    float cr = (float)r/255;
    float cg = (float)g/255;
    float cb = (float)b/255;
    float ca = (float)a/255;

    glClearColor(cr, cg, cb, ca);
}

// Clear used screen buffers (color and depth)
void rlClearScreenBuffers(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);     // Clear used buffers: Color and Depth (Depth is used for 3D)
    //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);     // Stencil buffer not used...
}

// Returns current OpenGL version
int rlGetVersion(void)
{
#if defined(GRAPHICS_API_OPENGL_11)
    return OPENGL_11;
#elif defined(GRAPHICS_API_OPENGL_33)
    return OPENGL_33;
#elif defined(GRAPHICS_API_OPENGL_ES2)
    return OPENGL_ES_20;
#endif
}

//----------------------------------------------------------------------------------
// Module Functions Definition - rlgl Functions
//----------------------------------------------------------------------------------

// Init OpenGL 3.3+ required data
void rlglInit(void)
{
    // Check OpenGL information and capabilities
    //------------------------------------------------------------------------------
    
    // Print current OpenGL and GLSL version
    TraceLog(INFO, "GPU: Vendor:   %s", glGetString(GL_VENDOR));
    TraceLog(INFO, "GPU: Renderer: %s", glGetString(GL_RENDERER));
    TraceLog(INFO, "GPU: Version:  %s", glGetString(GL_VERSION));
    TraceLog(INFO, "GPU: GLSL:     %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

    // NOTE: We can get a bunch of extra information about GPU capabilities (glGet*)
    //int maxTexSize;
    //glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    //TraceLog(INFO, "GL_MAX_TEXTURE_SIZE: %i", maxTexSize);
    
    //GL_MAX_TEXTURE_IMAGE_UNITS
    //GL_MAX_VIEWPORT_DIMS

    //int numAuxBuffers;
    //glGetIntegerv(GL_AUX_BUFFERS, &numAuxBuffers);
    //TraceLog(INFO, "GL_AUX_BUFFERS: %i", numAuxBuffers);
    
    //GLint numComp = 0;
    //GLint format[32] = { 0 };
    //glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &numComp);
    //glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, format);
    //for (int i = 0; i < numComp; i++) TraceLog(INFO, "Supported compressed format: 0x%x", format[i]);

    // NOTE: We don't need that much data on screen... right now...
    
#if defined(GRAPHICS_API_OPENGL_11)
    //TraceLog(INFO, "OpenGL 1.1 (or driver default) profile initialized");
#endif

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    // Get supported extensions list
    GLint numExt = 0;
    
#if defined(GRAPHICS_API_OPENGL_33)

    // NOTE: On OpenGL 3.3 VAO and NPOT are supported by default
    vaoSupported = true;
    npotSupported = true;

    // NOTE: We don't need to check again supported extensions but we do (in case GLEW is replaced sometime)
    // We get a list of available extensions and we check for some of them (compressed textures)
    glGetIntegerv(GL_NUM_EXTENSIONS, &numExt);
    const char *extList[numExt];
    
    for (int i = 0; i < numExt; i++) extList[i] = (char *)glGetStringi(GL_EXTENSIONS, i);
    
#elif defined(GRAPHICS_API_OPENGL_ES2)
    char *extensions = (char *)glGetString(GL_EXTENSIONS);  // One big const string
    
    // NOTE: We have to duplicate string because glGetString() returns a const value
    // If not duplicated, it fails in some systems (Raspberry Pi)
    // Equivalent to function: char *strdup(const char *str)
    char *extensionsDup;
    size_t len = strlen(extensions) + 1;
    void *newstr = malloc(len);
    if (newstr == NULL) extensionsDup = NULL;
    extensionsDup = (char *)memcpy(newstr, extensions, len);
    
    // NOTE: String could be splitted using strtok() function (string.h)
    // NOTE: strtok() modifies the received string, it can not be const
    
    char *extList[512];     // Allocate 512 strings pointers (2 KB)
    
    extList[numExt] = strtok(extensionsDup, " ");

    while (extList[numExt] != NULL)
    {
        numExt++;
        extList[numExt] = strtok(NULL, " ");
    }
    
    free(extensionsDup);    // Duplicated string must be deallocated
    
    numExt -= 1;
#endif

    TraceLog(INFO, "Number of supported extensions: %i", numExt);

    // Show supported extensions
    //for (int i = 0; i < numExt; i++)  TraceLog(INFO, "Supported extension: %s", extList[i]);

    // Check required extensions
    for (int i = 0; i < numExt; i++)
    {
#if defined(GRAPHICS_API_OPENGL_ES2)
        // Check VAO support
        // NOTE: Only check on OpenGL ES, OpenGL 3.3 has VAO support as core feature
        if (strcmp(extList[i], (const char *)"GL_OES_vertex_array_object") == 0)
        {
            vaoSupported = true;
            
            // The extension is supported by our hardware and driver, try to get related functions pointers           
            // NOTE: emscripten does not support VAOs natively, it uses emulation and it reduces overall performance...
            glGenVertexArrays = (PFNGLGENVERTEXARRAYSOESPROC)eglGetProcAddress("glGenVertexArraysOES");
            glBindVertexArray = (PFNGLBINDVERTEXARRAYOESPROC)eglGetProcAddress("glBindVertexArrayOES");
            glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSOESPROC)eglGetProcAddress("glDeleteVertexArraysOES");
            //glIsVertexArray = (PFNGLISVERTEXARRAYOESPROC)eglGetProcAddress("glIsVertexArrayOES");     // NOTE: Fails in WebGL, omitted
        }
        
        // Check NPOT textures support
        // NOTE: Only check on OpenGL ES, OpenGL 3.3 has NPOT textures full support as core feature
        if (strcmp(extList[i], (const char *)"GL_OES_texture_npot") == 0) npotSupported = true;
#endif   
        
        // DDS texture compression support
        if ((strcmp(extList[i], (const char *)"GL_EXT_texture_compression_s3tc") == 0) ||
            (strcmp(extList[i], (const char *)"GL_WEBGL_compressed_texture_s3tc") == 0) ||
            (strcmp(extList[i], (const char *)"GL_WEBKIT_WEBGL_compressed_texture_s3tc") == 0)) texCompDXTSupported = true; 
        
        // ETC1 texture compression support
        if ((strcmp(extList[i], (const char *)"GL_OES_compressed_ETC1_RGB8_texture") == 0) ||
            (strcmp(extList[i], (const char *)"GL_WEBGL_compressed_texture_etc1") == 0)) texCompETC1Supported = true;

        // ETC2/EAC texture compression support
        if (strcmp(extList[i], (const char *)"GL_ARB_ES3_compatibility") == 0) texCompETC2Supported = true;

        // PVR texture compression support
        if (strcmp(extList[i], (const char *)"GL_IMG_texture_compression_pvrtc") == 0) texCompPVRTSupported = true;

        // ASTC texture compression support
        if (strcmp(extList[i], (const char *)"GL_KHR_texture_compression_astc_hdr") == 0) texCompASTCSupported = true;
    }
    
#if defined(GRAPHICS_API_OPENGL_ES2)
    if (vaoSupported) TraceLog(INFO, "[EXTENSION] VAO extension detected, VAO functions initialized successfully");
    else TraceLog(WARNING, "[EXTENSION] VAO extension not found, VAO usage not supported");
    
    if (npotSupported) TraceLog(INFO, "[EXTENSION] NPOT textures extension detected, full NPOT textures supported");
    else TraceLog(WARNING, "[EXTENSION] NPOT textures extension not found, limited NPOT support (no-mipmaps, no-repeat)");
#endif

    if (texCompDXTSupported) TraceLog(INFO, "[EXTENSION] DXT compressed textures supported");
    if (texCompETC1Supported) TraceLog(INFO, "[EXTENSION] ETC1 compressed textures supported");
    if (texCompETC2Supported) TraceLog(INFO, "[EXTENSION] ETC2/EAC compressed textures supported");
    if (texCompPVRTSupported) TraceLog(INFO, "[EXTENSION] PVRT compressed textures supported");
    if (texCompASTCSupported) TraceLog(INFO, "[EXTENSION] ASTC compressed textures supported");

    // Initialize buffers, default shaders and default textures
    //----------------------------------------------------------
    
    // Set default draw mode
    currentDrawMode = RL_TRIANGLES;

    // Reset projection and modelview matrices
    projection = MatrixIdentity();
    modelview = MatrixIdentity();
    currentMatrix = &modelview;

    // Initialize matrix stack
    for (int i = 0; i < MATRIX_STACK_SIZE; i++) stack[i] = MatrixIdentity();
    
    // Create default white texture for plain colors (required by shader)
    unsigned char pixels[4] = { 255, 255, 255, 255 };   // 1 pixel RGBA (4 bytes)

    whiteTexture = rlglLoadTexture(pixels, 1, 1, UNCOMPRESSED_R8G8B8A8, 1);

    if (whiteTexture != 0) TraceLog(INFO, "[TEX ID %i] Base white texture loaded successfully", whiteTexture);
    else TraceLog(WARNING, "Base white texture could not be loaded");

    // Init default Shader (customized for GL 3.3 and ES2)
    defaultShader = LoadDefaultShader();
    currentShader = defaultShader;

    LoadDefaultBuffers();        // Initialize default vertex arrays buffers (lines, triangles, quads)

    // Init temp vertex buffer, used when transformation required (translate, rotate, scale)
    tempBuffer = (Vector3 *)malloc(sizeof(Vector3)*TEMP_VERTEX_BUFFER_SIZE);

    for (int i = 0; i < TEMP_VERTEX_BUFFER_SIZE; i++) tempBuffer[i] = VectorZero();

    // Init draw calls tracking system
    draws = (DrawCall *)malloc(sizeof(DrawCall)*MAX_DRAWS_BY_TEXTURE);

    for (int i = 0; i < MAX_DRAWS_BY_TEXTURE; i++)
    {
        draws[i].textureId = 0;
        draws[i].vertexCount = 0;
    }

    drawsCounter = 1;
    draws[drawsCounter - 1].textureId = whiteTexture;
#endif
}

// Vertex Buffer Object deinitialization (memory free)
void rlglClose(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    UnloadDefaultShader();
    UnloadDefaultBuffers();
    
    // Delete default white texture
    glDeleteTextures(1, &whiteTexture);
    TraceLog(INFO, "[TEX ID %i] Unloaded texture data (base white texture) from VRAM", whiteTexture);

    free(draws);
#endif
}

// Drawing batches: triangles, quads, lines
void rlglDraw(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    UpdateDefaultBuffers();

    if ((lines.vCounter > 0) || (triangles.vCounter > 0) || (quads.vCounter > 0))
    {
        glUseProgram(currentShader.id);
        
        Matrix matMVP = MatrixMultiply(modelview, projection);        // Create modelview-projection matrix

        glUniformMatrix4fv(currentShader.mvpLoc, 1, false, MatrixToFloat(matMVP));
        glUniform1i(currentShader.mapDiffuseLoc, 0);
        glUniform4f(currentShader.tintColorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    // NOTE: We draw in this order: lines, triangles, quads
    
    if (lines.vCounter > 0)
    {
        glBindTexture(GL_TEXTURE_2D, whiteTexture);

        if (vaoSupported)
        {
            glBindVertexArray(vaoLines);
        }
        else
        {
            glBindBuffer(GL_ARRAY_BUFFER, linesBuffer[0]);
            glVertexAttribPointer(currentShader.vertexLoc, 3, GL_FLOAT, 0, 0, 0);
            glEnableVertexAttribArray(currentShader.vertexLoc);

            if (currentShader.colorLoc != -1)
            {
                glBindBuffer(GL_ARRAY_BUFFER, linesBuffer[1]);
                glVertexAttribPointer(currentShader.colorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
                glEnableVertexAttribArray(currentShader.colorLoc);
            }
        }

        glDrawArrays(GL_LINES, 0, lines.vCounter);

        if (!vaoSupported) glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (triangles.vCounter > 0)
    {
        glBindTexture(GL_TEXTURE_2D, whiteTexture);

        if (vaoSupported)
        {
            glBindVertexArray(vaoTriangles);
        }
        else
        {
            glBindBuffer(GL_ARRAY_BUFFER, trianglesBuffer[0]);
            glVertexAttribPointer(currentShader.vertexLoc, 3, GL_FLOAT, 0, 0, 0);
            glEnableVertexAttribArray(currentShader.vertexLoc);

            if (currentShader.colorLoc != -1)
            {
                glBindBuffer(GL_ARRAY_BUFFER, trianglesBuffer[1]);
                glVertexAttribPointer(currentShader.colorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
                glEnableVertexAttribArray(currentShader.colorLoc);
            }
        }

        glDrawArrays(GL_TRIANGLES, 0, triangles.vCounter);

        if (!vaoSupported) glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (quads.vCounter > 0)
    {
        int quadsCount = 0;
        int numIndicesToProcess = 0;
        int indicesOffset = 0;

        if (vaoSupported)
        {
            glBindVertexArray(vaoQuads);
        }
        else
        {
            // Enable vertex attributes
            glBindBuffer(GL_ARRAY_BUFFER, quadsBuffer[0]);
            glVertexAttribPointer(currentShader.vertexLoc, 3, GL_FLOAT, 0, 0, 0);
            glEnableVertexAttribArray(currentShader.vertexLoc);

            glBindBuffer(GL_ARRAY_BUFFER, quadsBuffer[1]);
            glVertexAttribPointer(currentShader.texcoordLoc, 2, GL_FLOAT, 0, 0, 0);
            glEnableVertexAttribArray(currentShader.texcoordLoc);

            if (currentShader.colorLoc != -1)
            {
                glBindBuffer(GL_ARRAY_BUFFER, quadsBuffer[2]);
                glVertexAttribPointer(currentShader.colorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
                glEnableVertexAttribArray(currentShader.colorLoc);
            }
            
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadsBuffer[3]);
        }

        //TraceLog(DEBUG, "Draws required per frame: %i", drawsCounter);

        for (int i = 0; i < drawsCounter; i++)
        {
            quadsCount = draws[i].vertexCount/4;
            numIndicesToProcess = quadsCount*6;  // Get number of Quads * 6 index by Quad

            //TraceLog(DEBUG, "Quads to render: %i - Vertex Count: %i", quadsCount, draws[i].vertexCount);

            glBindTexture(GL_TEXTURE_2D, draws[i].textureId);

            // NOTE: The final parameter tells the GPU the offset in bytes from the start of the index buffer to the location of the first index to process
#if defined(GRAPHICS_API_OPENGL_33)
            glDrawElements(GL_TRIANGLES, numIndicesToProcess, GL_UNSIGNED_INT, (GLvoid*) (sizeof(GLuint) * indicesOffset));
#elif defined(GRAPHICS_API_OPENGL_ES2)
            glDrawElements(GL_TRIANGLES, numIndicesToProcess, GL_UNSIGNED_SHORT, (GLvoid*) (sizeof(GLushort) * indicesOffset));
#endif
            //GLenum err;
            //if ((err = glGetError()) != GL_NO_ERROR) TraceLog(INFO, "OpenGL error: %i", (int)err);    //GL_INVALID_ENUM!

            indicesOffset += draws[i].vertexCount/4*6;
        }

        if (!vaoSupported)
        {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }

        glBindTexture(GL_TEXTURE_2D, 0);  // Unbind textures
    }

    if (vaoSupported) glBindVertexArray(0);   // Unbind VAO

    glUseProgram(0);    // Unbind shader program

    // Reset draws counter
    drawsCounter = 1;
    draws[0].textureId = whiteTexture;
    draws[0].vertexCount = 0;

    // Reset vertex counters for next frame
    lines.vCounter = 0;
    lines.cCounter = 0;

    triangles.vCounter = 0;
    triangles.cCounter = 0;

    quads.vCounter = 0;
    quads.tcCounter = 0;
    quads.cCounter = 0;
    
    // Reset depth for next draw
    currentDepth = -1.0f;
#endif
}

// Draw a 3d model
// NOTE: Model transform can come within model struct
void rlglDrawModel(Model model, Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color color, bool wires)
{
#if defined (GRAPHICS_API_OPENGL_11) || defined(GRAPHICS_API_OPENGL_33)
    // NOTE: glPolygonMode() not available on OpenGL ES
    if (wires) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif

#if defined(GRAPHICS_API_OPENGL_11)
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, model.material.texDiffuse.id);

    // NOTE: On OpenGL 1.1 we use Vertex Arrays to draw model
    glEnableClientState(GL_VERTEX_ARRAY);                     // Enable vertex array
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);              // Enable texture coords array
    glEnableClientState(GL_NORMAL_ARRAY);                     // Enable normals array

    glVertexPointer(3, GL_FLOAT, 0, model.mesh.vertices);     // Pointer to vertex coords array
    glTexCoordPointer(2, GL_FLOAT, 0, model.mesh.texcoords);  // Pointer to texture coords array
    glNormalPointer(GL_FLOAT, 0, model.mesh.normals);         // Pointer to normals array
    //glColorPointer(4, GL_UNSIGNED_BYTE, 0, model.mesh.colors);   // Pointer to colors array (NOT USED)

    rlPushMatrix();
        rlTranslatef(position.x, position.y, position.z);
        rlScalef(scale.x, scale.y, scale.z);
        rlRotatef(rotationAngle, rotationAxis.x, rotationAxis.y, rotationAxis.z);

        rlColor4ub(color.r, color.g, color.b, color.a);

        glDrawArrays(GL_TRIANGLES, 0, model.mesh.vertexCount);
    rlPopMatrix();

    glDisableClientState(GL_VERTEX_ARRAY);                     // Disable vertex array
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);              // Disable texture coords array
    glDisableClientState(GL_NORMAL_ARRAY);                     // Disable normals array

    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
#endif

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glUseProgram(model.material.shader.id);
    
    // At this point the modelview matrix just contains the view matrix (camera)
    // That's because Begin3dMode() sets it an no model-drawing function modifies it, all use rlPushMatrix() and rlPopMatrix()
    Matrix matView = modelview;         // View matrix (camera)
    Matrix matProjection = projection;  // Projection matrix (perspective)
    
    // Calculate transformation matrix from function parameters
    // Get transform matrix (rotation -> scale -> translation)
    Matrix matRotation = MatrixRotate(rotationAxis, rotationAngle*DEG2RAD);
    Matrix matScale = MatrixScale(scale.x, scale.y, scale.z);
    Matrix matTranslation = MatrixTranslate(position.x, position.y, position.z);
    Matrix matTransform = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);
    
    // Combine model internal transformation matrix (model.transform) with matrix generated by function parameters (matTransform)
    Matrix matModel = MatrixMultiply(model.transform, matTransform);    // Transform to world-space coordinates
    
    // Calculate model-view matrix combining matModel and matView
    Matrix matModelView = MatrixMultiply(matModel, matView);            // Transform to camera-space coordinates

    // Calculate model-view-projection matrix (MVP)
    Matrix matMVP = MatrixMultiply(matModelView, matProjection);        // Transform to screen-space coordinates

    // Send combined model-view-projection matrix to shader
    glUniformMatrix4fv(model.material.shader.mvpLoc, 1, false, MatrixToFloat(matMVP));

    // Apply color tinting to model
    // NOTE: Just update one uniform on fragment shader
    float vColor[4] = { (float)color.r/255, (float)color.g/255, (float)color.b/255, (float)color.a/255 };
    glUniform4fv(model.material.shader.tintColorLoc, 1, vColor);

    // Set shader textures (diffuse, normal, specular)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, model.material.texDiffuse.id);
    glUniform1i(model.material.shader.mapDiffuseLoc, 0);        // Texture fits in active texture unit 0
    
    if ((model.material.texNormal.id != 0) && (model.material.shader.mapNormalLoc != -1))
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, model.material.texNormal.id);
        glUniform1i(model.material.shader.mapNormalLoc, 1);     // Texture fits in active texture unit 1
    }
    
    if ((model.material.texSpecular.id != 0) && (model.material.shader.mapSpecularLoc != -1))
    {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, model.material.texSpecular.id);
        glUniform1i(model.material.shader.mapSpecularLoc, 2);   // Texture fits in active texture unit 2
    }

    if (vaoSupported)
    {
        glBindVertexArray(model.mesh.vaoId);
    }
    else
    {
        // Bind model VBO data: vertex position
        glBindBuffer(GL_ARRAY_BUFFER, model.mesh.vboId[0]);
        glVertexAttribPointer(model.material.shader.vertexLoc, 3, GL_FLOAT, 0, 0, 0);
        glEnableVertexAttribArray(model.material.shader.vertexLoc);

        // Bind model VBO data: vertex texcoords
        glBindBuffer(GL_ARRAY_BUFFER, model.mesh.vboId[1]);
        glVertexAttribPointer(model.material.shader.texcoordLoc, 2, GL_FLOAT, 0, 0, 0);
        glEnableVertexAttribArray(model.material.shader.texcoordLoc);

        // Bind model VBO data: vertex normals (if available)
        if (model.material.shader.normalLoc != -1)
        {
            glBindBuffer(GL_ARRAY_BUFFER, model.mesh.vboId[2]);
            glVertexAttribPointer(model.material.shader.normalLoc, 3, GL_FLOAT, 0, 0, 0);
            glEnableVertexAttribArray(model.material.shader.normalLoc);
        }
        
        // TODO: Bind model VBO data: colors, tangents, texcoords2 (if available)
    }

    // Draw call!
    glDrawArrays(GL_TRIANGLES, 0, model.mesh.vertexCount);
    
    //glDisableVertexAttribArray(model.shader.vertexLoc);
    //glDisableVertexAttribArray(model.shader.texcoordLoc);
    //if (model.shader.normalLoc != -1) glDisableVertexAttribArray(model.shader.normalLoc);
    
    if (model.material.texNormal.id != 0)
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    
    if (model.material.texSpecular.id != 0)
    {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glActiveTexture(GL_TEXTURE0);               // Set shader active texture to default 0
    glBindTexture(GL_TEXTURE_2D, 0);            // Unbind textures

    if (vaoSupported) glBindVertexArray(0);     // Unbind VAO
    else glBindBuffer(GL_ARRAY_BUFFER, 0);      // Unbind VBOs

    glUseProgram(0);        // Unbind shader program
#endif

#if defined (GRAPHICS_API_OPENGL_11) || defined(GRAPHICS_API_OPENGL_33)
    // NOTE: glPolygonMode() not available on OpenGL ES
    if (wires) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
}

// Initialize Graphics Device (OpenGL stuff)
// NOTE: Stores global variables screenWidth and screenHeight
void rlglInitGraphics(int offsetX, int offsetY, int width, int height)
{   
    // NOTE: Required! viewport must be recalculated if screen resized!
    glViewport(offsetX/2, offsetY/2, width - offsetX, height - offsetY);    // Set viewport width and height

    // NOTE: Don't confuse glViewport with the transformation matrix
    // NOTE: glViewport just defines the area of the context that you will actually draw to.

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);                   // Set clear color (black)
    //glClearDepth(1.0f);                                   // Clear depth buffer (default)
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);     // Clear used buffers, depth buffer is used for 3D

    glDisable(GL_DEPTH_TEST);                               // Disable depth testing for 2D (only used for 3D)
    glDepthFunc(GL_LEQUAL);                                 // Type of depth testing to apply

    glEnable(GL_BLEND);                                     // Enable color blending (required to work with transparencies)
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);      // Color blending function (how colors are mixed)

#if defined(GRAPHICS_API_OPENGL_11)
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);      // Improve quality of color and texture coordinate interpolation (Deprecated in OGL 3.0)
                                                            // Other options: GL_FASTEST, GL_DONT_CARE (default)
#endif

    rlMatrixMode(RL_PROJECTION);                // Switch to PROJECTION matrix
    rlLoadIdentity();                           // Reset current matrix (PROJECTION)

    rlOrtho(0, width - offsetX, height - offsetY, 0, 0.0f, 1.0f); // Config orthographic mode: top-left corner --> (0,0)

    rlMatrixMode(RL_MODELVIEW);                 // Switch back to MODELVIEW matrix
    rlLoadIdentity();                           // Reset current matrix (MODELVIEW)

    // NOTE: All shapes/models triangles are drawn CCW

    glEnable(GL_CULL_FACE);       // Enable backface culling (Disabled by default)
    //glCullFace(GL_BACK);        // Cull the Back face (default)
    //glFrontFace(GL_CCW);        // Front face are defined counter clockwise (default)

#if defined(GRAPHICS_API_OPENGL_11)
    glShadeModel(GL_SMOOTH);      // Smooth shading between vertex (vertex colors interpolation) (Deprecated on OpenGL 3.3+)
                                  // Possible options: GL_SMOOTH (Color interpolation) or GL_FLAT (no interpolation)
#endif

    TraceLog(INFO, "OpenGL graphic device initialized successfully");
}

// Get world coordinates from screen coordinates
Vector3 rlglUnproject(Vector3 source, Matrix proj, Matrix view)
{
    Vector3 result = { 0.0f, 0.0f, 0.0f };
    
    // Calculate unproject matrix (multiply projection matrix and view matrix) and invert it
    Matrix matProjView = MatrixMultiply(proj, view);
    MatrixInvert(&matProjView);
    
    // Create quaternion from source point
    Quaternion quat = { source.x, source.y, source.z, 1.0f };
    
    // Multiply quat point by unproject matrix
    QuaternionTransform(&quat, matProjView);
    
    // Normalized world points in vectors
    result.x = quat.x/quat.w;
    result.y = quat.y/quat.w;
    result.z = quat.z/quat.w;

    return result;
}

// Convert image data to OpenGL texture (returns OpenGL valid Id)
unsigned int rlglLoadTexture(void *data, int width, int height, int textureFormat, int mipmapCount)
{
    glBindTexture(GL_TEXTURE_2D, 0);    // Free any old binding

    GLuint id = 0;
    
    // Check texture format support by OpenGL 1.1 (compressed textures not supported)
    if ((rlGetVersion() == OPENGL_11) && (textureFormat >= 8))
    {
        TraceLog(WARNING, "OpenGL 1.1 does not support GPU compressed texture formats");
        return id;
    }
    
    if ((!texCompDXTSupported) && ((textureFormat == COMPRESSED_DXT1_RGB) || (textureFormat == COMPRESSED_DXT1_RGBA) ||
        (textureFormat == COMPRESSED_DXT3_RGBA) || (textureFormat == COMPRESSED_DXT5_RGBA)))
    {
        TraceLog(WARNING, "DXT compressed texture format not supported");
        return id;
    }
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)    
    if ((!texCompETC1Supported) && (textureFormat == COMPRESSED_ETC1_RGB))
    {
        TraceLog(WARNING, "ETC1 compressed texture format not supported");
        return id;
    }
    
    if ((!texCompETC2Supported) && ((textureFormat == COMPRESSED_ETC2_RGB) || (textureFormat == COMPRESSED_ETC2_EAC_RGBA)))
    {
        TraceLog(WARNING, "ETC2 compressed texture format not supported");
        return id;
    }
    
    if ((!texCompPVRTSupported) && ((textureFormat == COMPRESSED_PVRT_RGB) || (textureFormat == COMPRESSED_PVRT_RGBA)))
    {
        TraceLog(WARNING, "PVRT compressed texture format not supported");
        return id;
    }
    
    if ((!texCompASTCSupported) && ((textureFormat == COMPRESSED_ASTC_4x4_RGBA) || (textureFormat == COMPRESSED_ASTC_8x8_RGBA)))
    {
        TraceLog(WARNING, "ASTC compressed texture format not supported");
        return id;
    }
#endif

    glGenTextures(1, &id);              // Generate Pointer to the texture

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    //glActiveTexture(GL_TEXTURE0);     // If not defined, using GL_TEXTURE0 by default (shader texture)
#endif

    glBindTexture(GL_TEXTURE_2D, id);

#if defined(GRAPHICS_API_OPENGL_33)
    // NOTE: We define internal (GPU) format as GL_RGBA8 (probably BGRA8 in practice, driver takes care)
    // NOTE: On embedded systems, we let the driver choose the best internal format

    // Support for multiple color modes (16bit color modes and grayscale)
    // (sized)internalFormat    format          type
    // GL_R                     GL_RED      GL_UNSIGNED_BYTE
    // GL_RGB565                GL_RGB      GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT_5_6_5
    // GL_RGB5_A1               GL_RGBA     GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT_5_5_5_1
    // GL_RGBA4                 GL_RGBA     GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT_4_4_4_4
    // GL_RGBA8                 GL_RGBA     GL_UNSIGNED_BYTE
    // GL_RGB8                  GL_RGB      GL_UNSIGNED_BYTE
    
    switch (textureFormat)
    {
        case UNCOMPRESSED_GRAYSCALE:
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, (unsigned char *)data);
            
            // With swizzleMask we define how a one channel texture will be mapped to RGBA
            // Required GL >= 3.3 or EXT_texture_swizzle/ARB_texture_swizzle
            GLint swizzleMask[] = { GL_RED, GL_RED, GL_RED, GL_ONE };
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
            
            TraceLog(INFO, "[TEX ID %i] Grayscale texture loaded and swizzled", id);
        } break;
        case UNCOMPRESSED_GRAY_ALPHA:
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width, height, 0, GL_RG, GL_UNSIGNED_BYTE, (unsigned char *)data);
            
            GLint swizzleMask[] = { GL_RED, GL_RED, GL_RED, GL_GREEN };
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
        } break;

        case UNCOMPRESSED_R5G6B5: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G5B5A1: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, (unsigned short *)data); break;
        case UNCOMPRESSED_R4G4B4A4: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA4, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8A8: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case COMPRESSED_DXT1_RGB: if (texCompDXTSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGB_S3TC_DXT1_EXT); break;
        case COMPRESSED_DXT1_RGBA: if (texCompDXTSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT); break;
        case COMPRESSED_DXT3_RGBA: if (texCompDXTSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT); break;
        case COMPRESSED_DXT5_RGBA: if (texCompDXTSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT); break;
        case COMPRESSED_ETC1_RGB: if (texCompETC1Supported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_ETC1_RGB8_OES); break;           // NOTE: Requires OpenGL ES 2.0 or OpenGL 4.3
        case COMPRESSED_ETC2_RGB: if (texCompETC2Supported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGB8_ETC2); break;    // NOTE: Requires OpenGL ES 3.0 or OpenGL 4.3
        case COMPRESSED_ETC2_EAC_RGBA: if (texCompETC2Supported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA8_ETC2_EAC); break;    // NOTE: Requires OpenGL ES 3.0 or OpenGL 4.3
        case COMPRESSED_PVRT_RGB: if (texCompPVRTSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG); break;        // NOTE: Requires PowerVR GPU
        case COMPRESSED_PVRT_RGBA: if (texCompPVRTSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG); break;     // NOTE: Requires PowerVR GPU
        case COMPRESSED_ASTC_4x4_RGBA: if (texCompASTCSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA_ASTC_4x4_KHR); break; // NOTE: Requires OpenGL ES 3.1 or OpenGL 4.3
        case COMPRESSED_ASTC_8x8_RGBA: if (texCompASTCSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA_ASTC_8x8_KHR); break; // NOTE: Requires OpenGL ES 3.1 or OpenGL 4.3
        default: TraceLog(WARNING, "Texture format not recognized"); break;
    }
#elif defined(GRAPHICS_API_OPENGL_11) || defined(GRAPHICS_API_OPENGL_ES2)
    // NOTE: on OpenGL ES 2.0 (WebGL), internalFormat must match format and options allowed are: GL_LUMINANCE, GL_RGB, GL_RGBA
    switch (textureFormat)
    {
        case UNCOMPRESSED_GRAYSCALE: glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_GRAY_ALPHA: glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, width, height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G6B5: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G5B5A1: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, (unsigned short *)data); break;
        case UNCOMPRESSED_R4G4B4A4: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8A8: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
#if defined(GRAPHICS_API_OPENGL_ES2)
        case COMPRESSED_DXT1_RGB: if (texCompDXTSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGB_S3TC_DXT1_EXT); break;
        case COMPRESSED_DXT1_RGBA: if (texCompDXTSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT); break;
        case COMPRESSED_DXT3_RGBA: if (texCompDXTSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT); break;     // NOTE: Not supported by WebGL
        case COMPRESSED_DXT5_RGBA: if (texCompDXTSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT); break;     // NOTE: Not supported by WebGL
        case COMPRESSED_ETC1_RGB: if (texCompETC1Supported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_ETC1_RGB8_OES); break;           // NOTE: Requires OpenGL ES 2.0 or OpenGL 4.3
        case COMPRESSED_ETC2_RGB: if (texCompETC2Supported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGB8_ETC2); break;    // NOTE: Requires OpenGL ES 3.0 or OpenGL 4.3
        case COMPRESSED_ETC2_EAC_RGBA: if (texCompETC2Supported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA8_ETC2_EAC); break;    // NOTE: Requires OpenGL ES 3.0 or OpenGL 4.3
        case COMPRESSED_PVRT_RGB: if (texCompPVRTSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG); break;        // NOTE: Requires PowerVR GPU
        case COMPRESSED_PVRT_RGBA: if (texCompPVRTSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG); break;     // NOTE: Requires PowerVR GPU
        case COMPRESSED_ASTC_4x4_RGBA: if (texCompASTCSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA_ASTC_4x4_KHR); break; // NOTE: Requires OpenGL ES 3.1 or OpenGL 4.3
        case COMPRESSED_ASTC_8x8_RGBA: if (texCompASTCSupported) LoadCompressedTexture((unsigned char *)data, width, height, mipmapCount, GL_COMPRESSED_RGBA_ASTC_8x8_KHR); break; // NOTE: Requires OpenGL ES 3.1 or OpenGL 4.3
#endif
        default: TraceLog(WARNING, "Texture format not supported"); break;
    }
#endif

    // Texture parameters configuration
    // NOTE: glTexParameteri does NOT affect texture uploading, just the way it's used
#if defined(GRAPHICS_API_OPENGL_ES2)
    // NOTE: OpenGL ES 2.0 with no GL_OES_texture_npot support (i.e. WebGL) has limited NPOT support, so CLAMP_TO_EDGE must be used
    if (npotSupported)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);       // Set texture to repeat on x-axis
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);       // Set texture to repeat on y-axis
    }
    else
    {
        // NOTE: If using negative texture coordinates (LoadOBJ()), it does not work!
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);       // Set texture to clamp on x-axis
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);       // Set texture to clamp on y-axis
    }
#else
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);       // Set texture to repeat on x-axis
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);       // Set texture to repeat on y-axis
#endif

    // Magnification and minification filters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);  // Alternative: GL_LINEAR
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);  // Alternative: GL_LINEAR
   
#if defined(GRAPHICS_API_OPENGL_33)
    if (mipmapCount > 1)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);   // Activate Trilinear filtering for mipmaps (must be available)
    }
#endif

    // At this point we have the texture loaded in GPU and texture parameters configured
    
    // NOTE: If mipmaps were not in data, they are not generated automatically

    // Unbind current texture
    glBindTexture(GL_TEXTURE_2D, 0);

    if (id > 0) TraceLog(INFO, "[TEX ID %i] Texture created successfully (%ix%i)", id, width, height);
    else TraceLog(WARNING, "Texture could not be created");

    return id;
}

// Load a texture to be used for rendering (fbo with color and depth attachments)
RenderTexture2D rlglLoadRenderTexture(int width, int height)
{
    RenderTexture2D target;
    
    target.id = 0;
    
    target.texture.id = 0;
    target.texture.width = width;
    target.texture.height = height;
    target.texture.format = UNCOMPRESSED_R8G8B8;
    target.texture.mipmaps = 1;
    
    target.depth.id = 0;
    target.depth.width = width;
    target.depth.height = height;
    target.depth.format = 19;       //DEPTH_COMPONENT_24BIT
    target.depth.mipmaps = 1;

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    // Create the texture that will serve as the color attachment for the framebuffer
    glGenTextures(1, &target.texture.id);
    glBindTexture(GL_TEXTURE_2D, target.texture.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    
#if defined(GRAPHICS_API_OPENGL_33)
    #define USE_DEPTH_TEXTURE
#else
    #define USE_DEPTH_RENDERBUFFER
#endif
    
#if defined(USE_DEPTH_RENDERBUFFER)
    // Create the renderbuffer that will serve as the depth attachment for the framebuffer.
    glGenRenderbuffers(1, &target.depth.id);
    glBindRenderbuffer(GL_RENDERBUFFER, target.depth.id);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);    // GL_DEPTH_COMPONENT24 not supported on Android
#elif defined(USE_DEPTH_TEXTURE)
    // NOTE: We can also use a texture for depth buffer (GL_ARB_depth_texture/GL_OES_depth_texture extension required)
    // A renderbuffer is simpler than a texture and could offer better performance on embedded devices
    glGenTextures(1, &target.depth.id);
    glBindTexture(GL_TEXTURE_2D, target.depth.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
#endif

    // Create the framebuffer object
    glGenFramebuffers(1, &target.id);
    glBindFramebuffer(GL_FRAMEBUFFER, target.id);

    // Attach color texture and depth renderbuffer to FBO
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture.id, 0);
#if defined(USE_DEPTH_RENDERBUFFER)
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, target.depth.id);
#elif defined(USE_DEPTH_TEXTURE)
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, target.depth.id, 0);
#endif

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        TraceLog(WARNING, "Framebuffer object could not be created...");
        
        switch(status)
        {
            case GL_FRAMEBUFFER_UNSUPPORTED: TraceLog(WARNING, "Framebuffer is unsupported"); break;
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: TraceLog(WARNING, "Framebuffer incomplete attachment"); break;
#if defined(GRAPHICS_API_OPENGL_ES2)
            case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS: TraceLog(WARNING, "Framebuffer incomplete dimensions"); break;
#endif
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: TraceLog(WARNING, "Framebuffer incomplete missing attachment"); break;
            default: break;
        }
        
        glDeleteTextures(1, &target.texture.id);
        glDeleteTextures(1, &target.depth.id);
        glDeleteFramebuffers(1, &target.id);
    }
    else TraceLog(INFO, "[FBO ID %i] Framebuffer object created successfully", target.id);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif

    return target; 
}

// Update already loaded texture in GPU with new data
void rlglUpdateTexture(unsigned int id, int width, int height, int format, void *data)
{
    glBindTexture(GL_TEXTURE_2D, id);

#if defined(GRAPHICS_API_OPENGL_33)
    switch (format)
    {
        case UNCOMPRESSED_GRAYSCALE: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_GRAY_ALPHA: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RG, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G6B5: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G5B5A1: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, (unsigned short *)data); break;
        case UNCOMPRESSED_R4G4B4A4: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8A8: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        default: TraceLog(WARNING, "Texture format updating not supported"); break;
    }
#elif defined(GRAPHICS_API_OPENGL_11) || defined(GRAPHICS_API_OPENGL_ES2)
    // NOTE: on OpenGL ES 2.0 (WebGL), internalFormat must match format and options allowed are: GL_LUMINANCE, GL_RGB, GL_RGBA
    switch (format)
    {
        case UNCOMPRESSED_GRAYSCALE: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_LUMINANCE, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_GRAY_ALPHA: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G6B5: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G5B5A1: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, (unsigned short *)data); break;
        case UNCOMPRESSED_R4G4B4A4: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8A8: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        default: TraceLog(WARNING, "Texture format updating not supported"); break;
    }
#endif
}

// Generate mipmap data for selected texture
void rlglGenerateMipmaps(Texture2D texture)
{
    glBindTexture(GL_TEXTURE_2D, texture.id);
    
    // Check if texture is power-of-two (POT)
    bool texIsPOT = false;
   
    if (((texture.width > 0) && ((texture.width & (texture.width - 1)) == 0)) && 
        ((texture.height > 0) && ((texture.height & (texture.height - 1)) == 0))) texIsPOT = true;

    if ((texIsPOT) || (npotSupported))
    {
#if defined(GRAPHICS_API_OPENGL_11)
        // Compute required mipmaps
        void *data = rlglReadTexturePixels(texture);
        
        // NOTE: data size is reallocated to fit mipmaps data
        // NOTE: CPU mipmap generation only supports RGBA 32bit data
        int mipmapCount = GenerateMipmaps(data, texture.width, texture.height);

        int size = texture.width*texture.height*4;  // RGBA 32bit only
        int offset = size;

        int mipWidth = texture.width/2;
        int mipHeight = texture.height/2;

        // Load the mipmaps
        for (int level = 1; level < mipmapCount; level++)
        {
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, mipWidth, mipHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, data + offset);

            size = mipWidth*mipHeight*4;
            offset += size;

            mipWidth /= 2;
            mipHeight /= 2;
        }
        
        TraceLog(WARNING, "[TEX ID %i] Mipmaps generated manually on CPU side", texture.id);
        
        // NOTE: Once mipmaps have been generated and data has been uploaded to GPU VRAM, we can discard RAM data
        free(data);
        
#endif

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
        glGenerateMipmap(GL_TEXTURE_2D);    // Generate mipmaps automatically
        TraceLog(INFO, "[TEX ID %i] Mipmaps generated automatically", texture.id);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);   // Activate Trilinear filtering for mipmaps (must be available)
#endif
    }
    else TraceLog(WARNING, "[TEX ID %i] Mipmaps can not be generated", texture.id);

    glBindTexture(GL_TEXTURE_2D, 0);
}

// Load vertex data into a VAO (if supported) and VBO
Model rlglLoadModel(Mesh mesh)
{
    Model model;

    model.mesh = mesh;
    model.mesh.vaoId = 0;       // Vertex Array Object
    model.mesh.vboId[0] = 0;    // Vertex positions VBO
    model.mesh.vboId[1] = 0;    // Vertex texcoords VBO
    model.mesh.vboId[2] = 0;    // Vertex normals VBO
    
    // TODO: Consider attributes: color, texcoords2, tangents (if available)
    
    model.transform = MatrixIdentity();

#if defined(GRAPHICS_API_OPENGL_11)
    model.material.texDiffuse.id = 0;    // No texture required
    model.material.shader.id = 0;        // No shader used

#elif defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    model.material.shader = defaultShader;          // Default model shader
    
    model.material.texDiffuse.id = whiteTexture;    // Default whiteTexture
    model.material.texDiffuse.width = 1;            // Default whiteTexture width
    model.material.texDiffuse.height = 1;           // Default whiteTexture height
    model.material.texDiffuse.format = UNCOMPRESSED_R8G8B8A8; // Default whiteTexture format
    
    model.material.texNormal.id = 0;        // By default, no normal texture
    model.material.texSpecular.id = 0;      // By default, no specular texture
    
    // TODO: Fill default material properties (color, glossiness...)
    
    GLuint vaoModel = 0;         // Vertex Array Objects (VAO)
    GLuint vertexBuffer[3];      // Vertex Buffer Objects (VBO)

    if (vaoSupported)
    {
        // Initialize Quads VAO (Buffer A)
        glGenVertexArrays(1, &vaoModel);
        glBindVertexArray(vaoModel);
    }

    // Create buffers for our vertex data (positions, texcoords, normals)
    glGenBuffers(3, vertexBuffer);
    
    // NOTE: Default shader is assigned to model, so vbo buffers are properly linked to vertex attribs
    // If model shader is changed, vbo buffers must be re-assigned to new location points (previously loaded)

    // Enable vertex attributes: position
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*mesh.vertexCount, mesh.vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(model.material.shader.vertexLoc, 3, GL_FLOAT, 0, 0, 0);
    glEnableVertexAttribArray(model.material.shader.vertexLoc);

    // Enable vertex attributes: texcoords
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*2*mesh.vertexCount, mesh.texcoords, GL_STATIC_DRAW);
    glVertexAttribPointer(model.material.shader.texcoordLoc, 2, GL_FLOAT, 0, 0, 0);
    glEnableVertexAttribArray(model.material.shader.texcoordLoc);

    // Enable vertex attributes: normals
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer[2]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*mesh.vertexCount, mesh.normals, GL_STATIC_DRAW);
    glVertexAttribPointer(model.material.shader.normalLoc, 3, GL_FLOAT, 0, 0, 0);
    glEnableVertexAttribArray(model.material.shader.normalLoc);
    
    glVertexAttrib4f(model.material.shader.colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);    // Color vertex attribute set to default: WHITE
    glDisableVertexAttribArray(model.material.shader.colorLoc);
    
    model.mesh.vboId[0] = vertexBuffer[0];     // Vertex position VBO
    model.mesh.vboId[1] = vertexBuffer[1];     // Texcoords VBO
    model.mesh.vboId[2] = vertexBuffer[2];     // Normals VBO

    if (vaoSupported)
    {
        if (vaoModel > 0)
        {
            model.mesh.vaoId = vaoModel;
            TraceLog(INFO, "[VAO ID %i] Model uploaded successfully to VRAM (GPU)", vaoModel);
        }
        else TraceLog(WARNING, "Model could not be uploaded to VRAM (GPU)");
    }
    else
    {
        TraceLog(INFO, "[VBO ID %i][VBO ID %i][VBO ID %i] Model uploaded successfully to VRAM (GPU)", model.mesh.vboId[0], model.mesh.vboId[1], model.mesh.vboId[2]);
    }
#endif

    return model;
}

// Read screen pixel data (color buffer)
unsigned char *rlglReadScreenPixels(int width, int height)
{
    unsigned char *screenData = (unsigned char *)malloc(width*height*sizeof(unsigned char)*4);

    // NOTE: glReadPixels returns image flipped vertically -> (0,0) is the bottom left corner of the framebuffer
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, screenData);

    // Flip image vertically!
    unsigned char *imgData = (unsigned char *)malloc(width*height*sizeof(unsigned char)*4);

    for (int y = height - 1; y >= 0; y--)
    {
        for (int x = 0; x < (width*4); x++)
        {
            // Flip line
            imgData[((height - 1) - y)*width*4 + x] = screenData[(y*width*4) + x];
            
            // Set alpha component value to 255 (no trasparent image retrieval)
            // NOTE: Alpha value has already been applied to RGB in framebuffer, we don't need it!
            if (((x + 1)%4) == 0) imgData[((height - 1) - y)*width*4 + x] = 255;
        }
    }

    free(screenData);

    return imgData;     // NOTE: image data should be freed
}

// Read texture pixel data
// NOTE: glGetTexImage() is not available on OpenGL ES 2.0
// Texture2D width and height are required on OpenGL ES 2.0. There is no way to get it from texture id.
void *rlglReadTexturePixels(Texture2D texture)
{
    void *pixels = NULL;
    
#if defined(GRAPHICS_API_OPENGL_11) || defined(GRAPHICS_API_OPENGL_33)
    glBindTexture(GL_TEXTURE_2D, texture.id);
    
    // NOTE: Using texture.id, we can retrieve some texture info (but not on OpenGL ES 2.0)
    /*
    int width, height, format;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
    // Other texture info: GL_TEXTURE_RED_SIZE, GL_TEXTURE_GREEN_SIZE, GL_TEXTURE_BLUE_SIZE, GL_TEXTURE_ALPHA_SIZE
    */
    
    int glFormat = 0, glType = 0;

    unsigned int size = texture.width*texture.height;
    
    // NOTE: GL_LUMINANCE and GL_LUMINANCE_ALPHA are removed since OpenGL 3.1
    // Must be replaced by GL_RED and GL_RG on Core OpenGL 3.3

    switch (texture.format)
    {
#if defined(GRAPHICS_API_OPENGL_11)
        case UNCOMPRESSED_GRAYSCALE: pixels = (unsigned char *)malloc(size); glFormat = GL_LUMINANCE; glType = GL_UNSIGNED_BYTE; break;            // 8 bit per pixel (no alpha)
        case UNCOMPRESSED_GRAY_ALPHA: pixels = (unsigned char *)malloc(size*2); glFormat = GL_LUMINANCE_ALPHA; glType = GL_UNSIGNED_BYTE; break;   // 16 bpp (2 channels)
#elif defined(GRAPHICS_API_OPENGL_33) 
        case UNCOMPRESSED_GRAYSCALE: pixels = (unsigned char *)malloc(size); glFormat = GL_RED; glType = GL_UNSIGNED_BYTE; break;       
        case UNCOMPRESSED_GRAY_ALPHA: pixels = (unsigned char *)malloc(size*2); glFormat = GL_RG; glType = GL_UNSIGNED_BYTE; break;
#endif
        case UNCOMPRESSED_R5G6B5: pixels = (unsigned short *)malloc(size); glFormat = GL_RGB; glType = GL_UNSIGNED_SHORT_5_6_5; break;             // 16 bpp
        case UNCOMPRESSED_R8G8B8: pixels = (unsigned char *)malloc(size*3); glFormat = GL_RGB; glType = GL_UNSIGNED_BYTE; break;                   // 24 bpp
        case UNCOMPRESSED_R5G5B5A1: pixels = (unsigned short *)malloc(size); glFormat = GL_RGBA; glType = GL_UNSIGNED_SHORT_5_5_5_1; break;        // 16 bpp (1 bit alpha)
        case UNCOMPRESSED_R4G4B4A4: pixels = (unsigned short *)malloc(size); glFormat = GL_RGBA; glType = GL_UNSIGNED_SHORT_4_4_4_4; break;        // 16 bpp (4 bit alpha)
        case UNCOMPRESSED_R8G8B8A8: pixels = (unsigned char *)malloc(size*4); glFormat = GL_RGBA; glType = GL_UNSIGNED_BYTE; break;                // 32 bpp
        default: TraceLog(WARNING, "Texture data retrieval, format not suported"); break;
    }
    
    // NOTE: Each row written to or read from by OpenGL pixel operations like glGetTexImage are aligned to a 4 byte boundary by default, which may add some padding.
    // Use glPixelStorei to modify padding with the GL_[UN]PACK_ALIGNMENT setting. 
    // GL_PACK_ALIGNMENT affects operations that read from OpenGL memory (glReadPixels, glGetTexImage, etc.) 
    // GL_UNPACK_ALIGNMENT affects operations that write to OpenGL memory (glTexImage, etc.)
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glGetTexImage(GL_TEXTURE_2D, 0, glFormat, glType, pixels);
    
    glBindTexture(GL_TEXTURE_2D, 0);
#endif

#if defined(GRAPHICS_API_OPENGL_ES2)

    RenderTexture2D fbo = rlglLoadRenderTexture(texture.width, texture.height);

    // NOTE: Two possible Options:
    // 1 - Bind texture to color fbo attachment and glReadPixels()
    // 2 - Create an fbo, activate it, render quad with texture, glReadPixels()
    
#define GET_TEXTURE_FBO_OPTION_1    // It works

#if defined(GET_TEXTURE_FBO_OPTION_1)
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.id);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Attach our texture to FBO -> Texture must be RGB
    // NOTE: Previoust attached texture is automatically detached
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id, 0);
    
    pixels = (unsigned char *)malloc(texture.width*texture.height*4*sizeof(unsigned char));
    
    // NOTE: Despite FBO color texture is RGB, we read data as RGBA... reading as RGB doesn't work... o__O
    glReadPixels(0, 0, texture.width, texture.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    
    // Re-attach internal FBO color texture before deleting it
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo.texture.id, 0);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
#elif defined(GET_TEXTURE_FBO_OPTION_2)
    // Render texture to fbo
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.id);
    
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, width, height);
    //glMatrixMode(GL_PROJECTION);
    //glLoadIdentity();
    rlOrtho(0.0, width, height, 0.0, 0.0, 1.0); 
    //glMatrixMode(GL_MODELVIEW);
    //glLoadIdentity();
    //glDisable(GL_TEXTURE_2D);
    //glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    
    Model quad;
    //quad.mesh = GenMeshQuad(width, height);
    quad.transform = MatrixIdentity();
    quad.shader = defaultShader;
    
    DrawModel(quad, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
    
    pixels = (unsigned char *)malloc(texture.width*texture.height*3*sizeof(unsigned char));
    
    glReadPixels(0, 0, texture.width, texture.height, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    // Bind framebuffer 0, which means render to back buffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    UnloadModel(quad);
#endif // GET_TEXTURE_FBO_OPTION

    // Clean up temporal fbo
    rlDeleteRenderTextures(fbo);

#endif

    return pixels;
}


//----------------------------------------------------------------------------------
// Module Functions Definition - Shaders Functions
// NOTE: Those functions are exposed directly to the user in raylib.h
//----------------------------------------------------------------------------------

// Load a custom shader and bind default locations
Shader LoadShader(char *vsFileName, char *fsFileName)
{
    Shader shader = { 0 };

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    // Shaders loading from external text file
    char *vShaderStr = ReadTextFile(vsFileName);
    char *fShaderStr = ReadTextFile(fsFileName);
    
    if ((vShaderStr != NULL) && (fShaderStr != NULL))
    {
        shader.id = LoadShaderProgram(vShaderStr, fShaderStr);

        // After shader loading, we try to load default location names
        if (shader.id != 0) LoadDefaultShaderLocations(&shader);
        
        // Shader strings must be freed
        free(vShaderStr);
        free(fShaderStr);
    }
    
    if (shader.id == 0)
    {
        TraceLog(WARNING, "Custom shader could not be loaded");
        shader = defaultShader;
    }        
#endif

    return shader;
}

// Load custom shader strings and return program id
unsigned int LoadShaderProgram(char *vShaderStr, char *fShaderStr)
{
    unsigned int program = 0;
	
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    GLuint vertexShader;
    GLuint fragmentShader;

    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

    const char *pvs = vShaderStr;
    const char *pfs = fShaderStr;

    glShaderSource(vertexShader, 1, &pvs, NULL);
    glShaderSource(fragmentShader, 1, &pfs, NULL);

    GLint success = 0;

    glCompileShader(vertexShader);

    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);

    if (success != GL_TRUE)
    {
        TraceLog(WARNING, "[VSHDR ID %i] Failed to compile vertex shader...", vertexShader);

        int maxLength = 0;
        int length;

        glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &maxLength);

        char log[maxLength];

        glGetShaderInfoLog(vertexShader, maxLength, &length, log);

        TraceLog(INFO, "%s", log);
    }
    else TraceLog(INFO, "[VSHDR ID %i] Vertex shader compiled successfully", vertexShader);

    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);

    if (success != GL_TRUE)
    {
        TraceLog(WARNING, "[FSHDR ID %i] Failed to compile fragment shader...", fragmentShader);

        int maxLength = 0;
        int length;

        glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &maxLength);

        char log[maxLength];

        glGetShaderInfoLog(fragmentShader, maxLength, &length, log);

        TraceLog(INFO, "%s", log);
    }
    else TraceLog(INFO, "[FSHDR ID %i] Fragment shader compiled successfully", fragmentShader);

    program = glCreateProgram();

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);

    glLinkProgram(program);
    
    // NOTE: All uniform variables are intitialised to 0 when a program links

    glGetProgramiv(program, GL_LINK_STATUS, &success);

    if (success == GL_FALSE)
    {
        TraceLog(WARNING, "[SHDR ID %i] Failed to link shader program...", program);

        int maxLength = 0;
        int length;

        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

        char log[maxLength];

        glGetProgramInfoLog(program, maxLength, &length, log);

        TraceLog(INFO, "%s", log);

        glDeleteProgram(program);

        program = 0;
    }
    else TraceLog(INFO, "[SHDR ID %i] Shader program loaded successfully", program);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
#endif
    return program;
}

// Unload a custom shader from memory
void UnloadShader(Shader shader)
{
    if (shader.id != 0)
    {
        rlDeleteShader(shader.id);
        TraceLog(INFO, "[SHDR ID %i] Unloaded shader program data", shader.id);
    }
}

// Set custom shader to be used on batch draw
void SetCustomShader(Shader shader)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    if (currentShader.id != shader.id)
    {
        rlglDraw();
        currentShader = shader;
    }
#endif
}

// Set default shader to be used in batch draw
void SetDefaultShader(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    SetCustomShader(defaultShader);
#endif
}

// Link shader to model
void SetModelShader(Model *model, Shader shader)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    model->material.shader = shader;

    if (vaoSupported) glBindVertexArray(model->mesh.vaoId);

    // Enable vertex attributes: position
    glBindBuffer(GL_ARRAY_BUFFER, model->mesh.vboId[0]);
    glEnableVertexAttribArray(shader.vertexLoc);
    glVertexAttribPointer(shader.vertexLoc, 3, GL_FLOAT, 0, 0, 0);

    // Enable vertex attributes: texcoords
    glBindBuffer(GL_ARRAY_BUFFER, model->mesh.vboId[1]);
    glEnableVertexAttribArray(shader.texcoordLoc);
    glVertexAttribPointer(shader.texcoordLoc, 2, GL_FLOAT, 0, 0, 0);

    // Enable vertex attributes: normals
    glBindBuffer(GL_ARRAY_BUFFER, model->mesh.vboId[2]);
    glEnableVertexAttribArray(shader.normalLoc);
    glVertexAttribPointer(shader.normalLoc, 3, GL_FLOAT, 0, 0, 0);

    if (vaoSupported) glBindVertexArray(0);     // Unbind VAO

#elif (GRAPHICS_API_OPENGL_11)
    TraceLog(WARNING, "Shaders not supported on OpenGL 1.1");
#endif
}

// Get shader uniform location
int GetShaderLocation(Shader shader, const char *uniformName)
{
    int location = -1;
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)   
    location = glGetUniformLocation(shader.id, uniformName);
    
    if (location == -1) TraceLog(WARNING, "[SHDR ID %i] Shader location for %s could not be found", shader.id, uniformName);
#endif
    return location;
}

// Set shader uniform value (float)
void SetShaderValue(Shader shader, int uniformLoc, float *value, int size)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glUseProgram(shader.id);

    if (size == 1) glUniform1fv(uniformLoc, 1, value);          // Shader uniform type: float
    else if (size == 2) glUniform2fv(uniformLoc, 1, value);     // Shader uniform type: vec2
    else if (size == 3) glUniform3fv(uniformLoc, 1, value);     // Shader uniform type: vec3
    else if (size == 4) glUniform4fv(uniformLoc, 1, value);     // Shader uniform type: vec4
    else TraceLog(WARNING, "Shader value float array size not supported");
    
    glUseProgram(0);
#endif
}

// Set shader uniform value (int)
void SetShaderValuei(Shader shader, int uniformLoc, int *value, int size)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glUseProgram(shader.id);

    if (size == 1) glUniform1iv(uniformLoc, 1, value);          // Shader uniform type: int
    else if (size == 2) glUniform2iv(uniformLoc, 1, value);     // Shader uniform type: ivec2
    else if (size == 3) glUniform3iv(uniformLoc, 1, value);     // Shader uniform type: ivec3
    else if (size == 4) glUniform4iv(uniformLoc, 1, value);     // Shader uniform type: ivec4
    else TraceLog(WARNING, "Shader value int array size not supported");
    
    glUseProgram(0);
#endif
}

// Set shader uniform value (matrix 4x4)
void SetShaderValueMatrix(Shader shader, int uniformLoc, Matrix mat)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glUseProgram(shader.id);

    glUniformMatrix4fv(uniformLoc, 1, false, MatrixToFloat(mat));
    
    glUseProgram(0);
#endif
}

// Set blending mode (alpha, additive, multiplied)
// NOTE: Only 3 blending modes predefined
void SetBlendMode(int mode)
{
    if ((blendMode != mode) && (mode < 3))
    {
        rlglDraw();
        
        switch (mode)
        {
            case BLEND_ALPHA: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
            case BLEND_ADDITIVE: glBlendFunc(GL_SRC_ALPHA, GL_ONE); break; // Alternative: glBlendFunc(GL_ONE, GL_ONE);
            case BLEND_MULTIPLIED: glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA); break;
            default: break;
        }
        
        blendMode = mode;
    }
}

//----------------------------------------------------------------------------------
// Module specific Functions Definition
//----------------------------------------------------------------------------------

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
// Convert image data to OpenGL texture (returns OpenGL valid Id)
// NOTE: Expected compressed image data and POT image
static void LoadCompressedTexture(unsigned char *data, int width, int height, int mipmapCount, int compressedFormat)
{
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    int blockSize = 0;      // Bytes every block
    int offset = 0;

    if ((compressedFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT) ||
        (compressedFormat == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) ||
#if defined(GRAPHICS_API_OPENGL_ES2)
        (compressedFormat == GL_ETC1_RGB8_OES) ||
#endif
        (compressedFormat == GL_COMPRESSED_RGB8_ETC2)) blockSize = 8;
    else blockSize = 16;

    // Load the mipmap levels
    for (int level = 0; level < mipmapCount && (width || height); level++)
    {
        unsigned int size = 0;
        
        size = ((width + 3)/4)*((height + 3)/4)*blockSize;

        glCompressedTexImage2D(GL_TEXTURE_2D, level, compressedFormat, width, height, 0, size, data + offset);

        offset += size;
        width  /= 2;
        height /= 2;

        // Security check for NPOT textures
        if (width < 1) width = 1;
        if (height < 1) height = 1;
    }
}

// Load default shader (Vertex and Fragment)
// NOTE: This shader program is used for batch buffers (lines, triangles, quads)
static Shader LoadDefaultShader(void)
{
    Shader shader;

    // Vertex shader directly defined, no external file required
#if defined(GRAPHICS_API_OPENGL_33)
    char vShaderStr[] = "#version 330       \n"
        "in vec3 vertexPosition;            \n"
        "in vec2 vertexTexCoord;            \n"
        "in vec4 vertexColor;               \n"
        "out vec2 fragTexCoord;             \n"
        "out vec4 fragColor;                \n"
#elif defined(GRAPHICS_API_OPENGL_ES2)
    char vShaderStr[] = "#version 100       \n"
        "attribute vec3 vertexPosition;     \n"
        "attribute vec2 vertexTexCoord;     \n"
        "attribute vec4 vertexColor;        \n"
        "varying vec2 fragTexCoord;         \n"
        "varying vec4 fragColor;            \n"
#endif
        "uniform mat4 mvpMatrix;            \n"
        "void main()                        \n"
        "{                                  \n"
        "    fragTexCoord = vertexTexCoord; \n"
        "    fragColor = vertexColor;       \n"
        "    gl_Position = mvpMatrix*vec4(vertexPosition, 1.0); \n"
        "}                                  \n";

    // Fragment shader directly defined, no external file required
#if defined(GRAPHICS_API_OPENGL_33)
    char fShaderStr[] = "#version 330       \n"
        "in vec2 fragTexCoord;              \n"
        "in vec4 fragColor;                 \n"
        "out vec4 finalColor;               \n"
#elif defined(GRAPHICS_API_OPENGL_ES2)
    char fShaderStr[] = "#version 100       \n"
        "precision mediump float;           \n"     // precision required for OpenGL ES2 (WebGL)
        "varying vec2 fragTexCoord;         \n"
        "varying vec4 fragColor;            \n"
#endif
        "uniform sampler2D texture0;        \n"
        "uniform vec4 fragTintColor;        \n"
        "void main()                        \n"
        "{                                  \n"
#if defined(GRAPHICS_API_OPENGL_33)
        "    vec4 texelColor = texture(texture0, fragTexCoord);   \n"
        "    finalColor = texelColor*fragTintColor*fragColor;     \n"
#elif defined(GRAPHICS_API_OPENGL_ES2)
        "    vec4 texelColor = texture2D(texture0, fragTexCoord); \n" // NOTE: texture2D() is deprecated on OpenGL 3.3 and ES 3.0
        "    gl_FragColor = texelColor*fragTintColor*fragColor;   \n"
#endif
        "}                                  \n";

    shader.id = LoadShaderProgram(vShaderStr, fShaderStr);

    if (shader.id != 0) TraceLog(INFO, "[SHDR ID %i] Default shader loaded successfully", shader.id);
    else TraceLog(WARNING, "[SHDR ID %i] Default shader could not be loaded", shader.id);

    if (shader.id != 0) LoadDefaultShaderLocations(&shader);

    return shader;
}

// Get location handlers to for shader attributes and uniforms
// NOTE: If any location is not found, loc point becomes -1
static void LoadDefaultShaderLocations(Shader *shader)
{
    // Get handles to GLSL input attibute locations
    shader->vertexLoc = glGetAttribLocation(shader->id, "vertexPosition");
    shader->texcoordLoc = glGetAttribLocation(shader->id, "vertexTexCoord");
    shader->normalLoc = glGetAttribLocation(shader->id, "vertexNormal");
    shader->colorLoc = glGetAttribLocation(shader->id, "vertexColor");

    // Get handles to GLSL uniform locations (vertex shader)
    shader->mvpLoc  = glGetUniformLocation(shader->id, "mvpMatrix");

    // Get handles to GLSL uniform locations (fragment shader)
    shader->tintColorLoc = glGetUniformLocation(shader->id, "fragTintColor");
    shader->mapDiffuseLoc = glGetUniformLocation(shader->id, "texture0");
    shader->mapNormalLoc = glGetUniformLocation(shader->id, "texture1");
    shader->mapSpecularLoc = glGetUniformLocation(shader->id, "texture2");
}

// Unload default shader 
static void UnloadDefaultShader(void)
{
    glUseProgram(0);

    //glDetachShader(defaultShaderProgram, vertexShader);
    //glDetachShader(defaultShaderProgram, fragmentShader);
    //glDeleteShader(vertexShader);     // Already deleted on shader compilation
    //glDeleteShader(fragmentShader);   // Already deleted on sahder compilation
    glDeleteProgram(defaultShader.id);
}

// Load default internal buffers (lines, triangles, quads)
static void LoadDefaultBuffers(void)
{
    // [CPU] Allocate and initialize float array buffers to store vertex data (lines, triangles, quads)
    //--------------------------------------------------------------------------------------------
    
    // Initialize lines arrays (vertex position and color data)
    lines.vertices = (float *)malloc(sizeof(float)*3*2*MAX_LINES_BATCH);        // 3 float by vertex, 2 vertex by line
    lines.colors = (unsigned char *)malloc(sizeof(unsigned char)*4*2*MAX_LINES_BATCH);  // 4 float by color, 2 colors by line

    for (int i = 0; i < (3*2*MAX_LINES_BATCH); i++) lines.vertices[i] = 0.0f;
    for (int i = 0; i < (4*2*MAX_LINES_BATCH); i++) lines.colors[i] = 0;

    lines.vCounter = 0;
    lines.cCounter = 0;

    // Initialize triangles arrays (vertex position and color data)
    triangles.vertices = (float *)malloc(sizeof(float)*3*3*MAX_TRIANGLES_BATCH);        // 3 float by vertex, 3 vertex by triangle
    triangles.colors = (unsigned char *)malloc(sizeof(unsigned char)*4*3*MAX_TRIANGLES_BATCH);  // 4 float by color, 3 colors by triangle

    for (int i = 0; i < (3*3*MAX_TRIANGLES_BATCH); i++) triangles.vertices[i] = 0.0f;
    for (int i = 0; i < (4*3*MAX_TRIANGLES_BATCH); i++) triangles.colors[i] = 0;

    triangles.vCounter = 0;
    triangles.cCounter = 0;

    // Initialize quads arrays (vertex position, texcoord and color data... and indexes)
    quads.vertices = (float *)malloc(sizeof(float)*3*4*MAX_QUADS_BATCH);        // 3 float by vertex, 4 vertex by quad
    quads.texcoords = (float *)malloc(sizeof(float)*2*4*MAX_QUADS_BATCH);       // 2 float by texcoord, 4 texcoord by quad
    quads.colors = (unsigned char *)malloc(sizeof(unsigned char)*4*4*MAX_QUADS_BATCH);  // 4 float by color, 4 colors by quad
#if defined(GRAPHICS_API_OPENGL_33)
    quads.indices = (unsigned int *)malloc(sizeof(int)*6*MAX_QUADS_BATCH);      // 6 int by quad (indices)
#elif defined(GRAPHICS_API_OPENGL_ES2)
    quads.indices = (unsigned short *)malloc(sizeof(short)*6*MAX_QUADS_BATCH);  // 6 int by quad (indices)
#endif

    for (int i = 0; i < (3*4*MAX_QUADS_BATCH); i++) quads.vertices[i] = 0.0f;
    for (int i = 0; i < (2*4*MAX_QUADS_BATCH); i++) quads.texcoords[i] = 0.0f;
    for (int i = 0; i < (4*4*MAX_QUADS_BATCH); i++) quads.colors[i] = 0;

    int k = 0;

    // Indices can be initialized right now
    for (int i = 0; i < (6*MAX_QUADS_BATCH); i+=6)
    {
        quads.indices[i] = 4*k;
        quads.indices[i+1] = 4*k+1;
        quads.indices[i+2] = 4*k+2;
        quads.indices[i+3] = 4*k;
        quads.indices[i+4] = 4*k+2;
        quads.indices[i+5] = 4*k+3;

        k++;
    }

    quads.vCounter = 0;
    quads.tcCounter = 0;
    quads.cCounter = 0;

    TraceLog(INFO, "Default buffers initialized successfully in CPU (lines, triangles, quads)");
    //--------------------------------------------------------------------------------------------
    
    // [GPU] Upload vertex data and initialize VAOs/VBOs (lines, triangles, quads)
    // NOTE: Default buffers are linked to use currentShader (defaultShader)
    //--------------------------------------------------------------------------------------------
    
    // Upload and link lines vertex buffers
    if (vaoSupported)
    {
        // Initialize Lines VAO
        glGenVertexArrays(1, &vaoLines);
        glBindVertexArray(vaoLines);
    }

    // Create buffers for our vertex data
    glGenBuffers(2, linesBuffer);

    // Lines - Vertex positions buffer binding and attributes enable
    glBindBuffer(GL_ARRAY_BUFFER, linesBuffer[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*2*MAX_LINES_BATCH, lines.vertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.vertexLoc);
    glVertexAttribPointer(currentShader.vertexLoc, 3, GL_FLOAT, 0, 0, 0);

    // Lines - colors buffer
    glBindBuffer(GL_ARRAY_BUFFER, linesBuffer[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(unsigned char)*4*2*MAX_LINES_BATCH, lines.colors, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.colorLoc);
    glVertexAttribPointer(currentShader.colorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);

    if (vaoSupported) TraceLog(INFO, "[VAO ID %i] Default buffers (lines) VAO initialized successfully", vaoLines);
    else TraceLog(INFO, "[VBO ID %i][VBO ID %i] Default buffers (lines) VBOs initialized successfully", linesBuffer[0], linesBuffer[1]);

    // Upload and link triangles vertex buffers
    if (vaoSupported)
    {
        // Initialize Triangles VAO
        glGenVertexArrays(1, &vaoTriangles);
        glBindVertexArray(vaoTriangles);
    }

    // Create buffers for our vertex data
    glGenBuffers(2, trianglesBuffer);

    // Enable vertex attributes
    glBindBuffer(GL_ARRAY_BUFFER, trianglesBuffer[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*3*MAX_TRIANGLES_BATCH, triangles.vertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.vertexLoc);
    glVertexAttribPointer(currentShader.vertexLoc, 3, GL_FLOAT, 0, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, trianglesBuffer[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(unsigned char)*4*3*MAX_TRIANGLES_BATCH, triangles.colors, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.colorLoc);
    glVertexAttribPointer(currentShader.colorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);

    if (vaoSupported) TraceLog(INFO, "[VAO ID %i] Default buffers (triangles) VAO initialized successfully", vaoTriangles);
    else TraceLog(INFO, "[VBO ID %i][VBO ID %i] Default buffers (triangles) VBOs initialized successfully", trianglesBuffer[0], trianglesBuffer[1]);

    // Upload and link quads vertex buffers
    if (vaoSupported)
    {
        // Initialize Quads VAO
        glGenVertexArrays(1, &vaoQuads);
        glBindVertexArray(vaoQuads);
    }

    // Create buffers for our vertex data
    glGenBuffers(4, quadsBuffer);

    // Enable vertex attributes
    glBindBuffer(GL_ARRAY_BUFFER, quadsBuffer[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*4*MAX_QUADS_BATCH, quads.vertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.vertexLoc);
    glVertexAttribPointer(currentShader.vertexLoc, 3, GL_FLOAT, 0, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, quadsBuffer[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*2*4*MAX_QUADS_BATCH, quads.texcoords, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.texcoordLoc);
    glVertexAttribPointer(currentShader.texcoordLoc, 2, GL_FLOAT, 0, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, quadsBuffer[2]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(unsigned char)*4*4*MAX_QUADS_BATCH, quads.colors, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.colorLoc);
    glVertexAttribPointer(currentShader.colorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);

    // Fill index buffer
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadsBuffer[3]);
#if defined(GRAPHICS_API_OPENGL_33)
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(int)*6*MAX_QUADS_BATCH, quads.indices, GL_STATIC_DRAW);
#elif defined(GRAPHICS_API_OPENGL_ES2)
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(short)*6*MAX_QUADS_BATCH, quads.indices, GL_STATIC_DRAW);
#endif

    if (vaoSupported) TraceLog(INFO, "[VAO ID %i] Default buffers (quads) VAO initialized successfully", vaoQuads);
    else TraceLog(INFO, "[VBO ID %i][VBO ID %i][VBO ID %i][VBO ID %i] Default buffers (quads) VBOs initialized successfully", quadsBuffer[0], quadsBuffer[1], quadsBuffer[2], quadsBuffer[3]);

    // Unbind the current VAO
    if (vaoSupported) glBindVertexArray(0);
    //--------------------------------------------------------------------------------------------
}

// Update default buffers (VAOs/VBOs) with vertex array data
// NOTE: If there is not vertex data, buffers doesn't need to be updated (vertexCount > 0)
// TODO: If no data changed on the CPU arrays --> No need to re-update GPU arrays (change flag required)
static void UpdateDefaultBuffers(void)
{
    // Update lines vertex buffers
    if (lines.vCounter > 0)
    {
        // Activate Lines VAO
        if (vaoSupported) glBindVertexArray(vaoLines);

        // Lines - vertex positions buffer
        glBindBuffer(GL_ARRAY_BUFFER, linesBuffer[0]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*2*MAX_LINES_BATCH, lines.vertices, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float)*3*lines.vCounter, lines.vertices);    // target - offset (in bytes) - size (in bytes) - data pointer

        // Lines - colors buffer
        glBindBuffer(GL_ARRAY_BUFFER, linesBuffer[1]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*4*2*MAX_LINES_BATCH, lines.colors, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(unsigned char)*4*lines.cCounter, lines.colors);
    }

    // Update triangles vertex buffers
    if (triangles.vCounter > 0)
    {
        // Activate Triangles VAO
        if (vaoSupported) glBindVertexArray(vaoTriangles);

        // Triangles - vertex positions buffer
        glBindBuffer(GL_ARRAY_BUFFER, trianglesBuffer[0]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*3*MAX_TRIANGLES_BATCH, triangles.vertices, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float)*3*triangles.vCounter, triangles.vertices);

        // Triangles - colors buffer
        glBindBuffer(GL_ARRAY_BUFFER, trianglesBuffer[1]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*4*3*MAX_TRIANGLES_BATCH, triangles.colors, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(unsigned char)*4*triangles.cCounter, triangles.colors);
    }

    // Update quads vertex buffers
    if (quads.vCounter > 0)
    {
        // Activate Quads VAO
        if (vaoSupported) glBindVertexArray(vaoQuads);

        // Quads - vertex positions buffer
        glBindBuffer(GL_ARRAY_BUFFER, quadsBuffer[0]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*4*MAX_QUADS_BATCH, quads.vertices, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float)*3*quads.vCounter, quads.vertices);

        // Quads - texture coordinates buffer
        glBindBuffer(GL_ARRAY_BUFFER, quadsBuffer[1]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*2*4*MAX_QUADS_BATCH, quads.texcoords, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float)*2*quads.vCounter, quads.texcoords);

        // Quads - colors buffer
        glBindBuffer(GL_ARRAY_BUFFER, quadsBuffer[2]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*4*4*MAX_QUADS_BATCH, quads.colors, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(unsigned char)*4*quads.vCounter, quads.colors);

        // Another option would be using buffer mapping...
        //quads.vertices = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);
        // Now we can modify vertices
        //glUnmapBuffer(GL_ARRAY_BUFFER);
    }
    //--------------------------------------------------------------

    // Unbind the current VAO
    if (vaoSupported) glBindVertexArray(0);
}

// Unload default buffers vertex data from CPU and GPU
static void UnloadDefaultBuffers(void)
{
    // Unbind everything
    if (vaoSupported) glBindVertexArray(0);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Delete VBOs from GPU (VRAM)
    glDeleteBuffers(1, &linesBuffer[0]);
    glDeleteBuffers(1, &linesBuffer[1]);
    glDeleteBuffers(1, &trianglesBuffer[0]);
    glDeleteBuffers(1, &trianglesBuffer[1]);
    glDeleteBuffers(1, &quadsBuffer[0]);
    glDeleteBuffers(1, &quadsBuffer[1]);
    glDeleteBuffers(1, &quadsBuffer[2]);
    glDeleteBuffers(1, &quadsBuffer[3]);

    if (vaoSupported)
    {
        // Delete VAOs from GPU (VRAM)
        glDeleteVertexArrays(1, &vaoLines);
        glDeleteVertexArrays(1, &vaoTriangles);
        glDeleteVertexArrays(1, &vaoQuads);
    }

    // Free vertex arrays memory from CPU (RAM)
    free(lines.vertices);
    free(lines.colors);

    free(triangles.vertices);
    free(triangles.colors);

    free(quads.vertices);
    free(quads.texcoords);
    free(quads.colors);
    free(quads.indices);
}

// Read text data from file
// NOTE: text chars array should be freed manually
static char *ReadTextFile(const char *fileName)
{
    FILE *textFile;
    char *text = NULL;

    int count = 0;

    if (fileName != NULL)
    {
        textFile = fopen(fileName,"rt");

        if (textFile != NULL)
        {
            fseek(textFile, 0, SEEK_END);
            count = ftell(textFile);
            rewind(textFile);

            if (count > 0)
            {
                text = (char *)malloc(sizeof(char)*(count + 1));
                count = fread(text, sizeof(char), count, textFile);
                text[count] = '\0';
            }

            fclose(textFile);
        }
        else TraceLog(WARNING, "[%s] Text file could not be opened", fileName);
    }

    return text;
}
#endif //defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)

#if defined(GRAPHICS_API_OPENGL_11)
// Mipmaps data is generated after image data
static int GenerateMipmaps(unsigned char *data, int baseWidth, int baseHeight)
{
    int mipmapCount = 1;                // Required mipmap levels count (including base level)
    int width = baseWidth;
    int height = baseHeight;
    int size = baseWidth*baseHeight*4;  // Size in bytes (will include mipmaps...), RGBA only

    // Count mipmap levels required
    while ((width != 1) && (height != 1))
    {
        if (width != 1) width /= 2;
        if (height != 1) height /= 2;

        TraceLog(DEBUG, "Next mipmap size: %i x %i", width, height);

        mipmapCount++;

        size += (width*height*4);       // Add mipmap size (in bytes)
    }

    TraceLog(DEBUG, "Total mipmaps required: %i", mipmapCount);
    TraceLog(DEBUG, "Total size of data required: %i", size);

    unsigned char *temp = realloc(data, size);

    if (temp != NULL) data = temp;
    else TraceLog(WARNING, "Mipmaps required memory could not be allocated");

    width = baseWidth;
    height = baseHeight;
    size = (width*height*4);

    // Generate mipmaps
    // NOTE: Every mipmap data is stored after data
    Color *image = (Color *)malloc(width*height*sizeof(Color));
    Color *mipmap = NULL;
    int offset = 0;
    int j = 0;

    for (int i = 0; i < size; i += 4)
    {
        image[j].r = data[i];
        image[j].g = data[i + 1];
        image[j].b = data[i + 2];
        image[j].a = data[i + 3];
        j++;
    }

    TraceLog(DEBUG, "Mipmap base (%ix%i)", width, height);

    for (int mip = 1; mip < mipmapCount; mip++)
    {
        mipmap = GenNextMipmap(image, width, height);

        offset += (width*height*4); // Size of last mipmap
        j = 0;

        width /= 2;
        height /= 2;
        size = (width*height*4);    // Mipmap size to store after offset

        // Add mipmap to data
        for (int i = 0; i < size; i += 4)
        {
            data[offset + i] = mipmap[j].r;
            data[offset + i + 1] = mipmap[j].g;
            data[offset + i + 2] = mipmap[j].b;
            data[offset + i + 3] = mipmap[j].a;
            j++;
        }

        free(image);

        image = mipmap;
        mipmap = NULL;
    }

    free(mipmap);       // free mipmap data

    return mipmapCount;
}

// Manual mipmap generation (basic scaling algorithm)
static Color *GenNextMipmap(Color *srcData, int srcWidth, int srcHeight)
{
    int x2, y2;
    Color prow, pcol;

    int width = srcWidth/2;
    int height = srcHeight/2;

    Color *mipmap = (Color *)malloc(width*height*sizeof(Color));

    // Scaling algorithm works perfectly (box-filter)
    for (int y = 0; y < height; y++)
    {
        y2 = 2*y;

        for (int x = 0; x < width; x++)
        {
            x2 = 2*x;

            prow.r = (srcData[y2*srcWidth + x2].r + srcData[y2*srcWidth + x2 + 1].r)/2;
            prow.g = (srcData[y2*srcWidth + x2].g + srcData[y2*srcWidth + x2 + 1].g)/2;
            prow.b = (srcData[y2*srcWidth + x2].b + srcData[y2*srcWidth + x2 + 1].b)/2;
            prow.a = (srcData[y2*srcWidth + x2].a + srcData[y2*srcWidth + x2 + 1].a)/2;

            pcol.r = (srcData[(y2+1)*srcWidth + x2].r + srcData[(y2+1)*srcWidth + x2 + 1].r)/2;
            pcol.g = (srcData[(y2+1)*srcWidth + x2].g + srcData[(y2+1)*srcWidth + x2 + 1].g)/2;
            pcol.b = (srcData[(y2+1)*srcWidth + x2].b + srcData[(y2+1)*srcWidth + x2 + 1].b)/2;
            pcol.a = (srcData[(y2+1)*srcWidth + x2].a + srcData[(y2+1)*srcWidth + x2 + 1].a)/2;

            mipmap[y*width + x].r = (prow.r + pcol.r)/2;
            mipmap[y*width + x].g = (prow.g + pcol.g)/2;
            mipmap[y*width + x].b = (prow.b + pcol.b)/2;
            mipmap[y*width + x].a = (prow.a + pcol.a)/2;
        }
    }

    TraceLog(DEBUG, "Mipmap generated successfully (%ix%i)", width, height);

    return mipmap;
}
#endif

#if defined(RLGL_STANDALONE)
// Output a trace log message
// NOTE: Expected msgType: (0)Info, (1)Error, (2)Warning
static void TraceLog(int msgType, const char *text, ...)
{
    va_list args;
    va_start(args, text);

    switch(msgType)
    {
        case INFO: fprintf(stdout, "INFO: "); break;
        case ERROR: fprintf(stdout, "ERROR: "); break;
        case WARNING: fprintf(stdout, "WARNING: "); break;
        case DEBUG: fprintf(stdout, "DEBUG: "); break;
        default: break;
    }

    vfprintf(stdout, text, args);
    fprintf(stdout, "\n");

    va_end(args);

    if (msgType == ERROR) exit(1);
}

// Converts Matrix to float array
// NOTE: Returned vector is a transposed version of the Matrix struct, 
// it should be this way because, despite raymath use OpenGL column-major convention,
// Matrix struct memory alignment and variables naming are not coherent
float *MatrixToFloat(Matrix mat)
{
    static float buffer[16];

    buffer[0] = mat.m0;
    buffer[1] = mat.m4;
    buffer[2] = mat.m8;
    buffer[3] = mat.m12;
    buffer[4] = mat.m1;
    buffer[5] = mat.m5;
    buffer[6] = mat.m9;
    buffer[7] = mat.m13;
    buffer[8] = mat.m2;
    buffer[9] = mat.m6;
    buffer[10] = mat.m10;
    buffer[11] = mat.m14;
    buffer[12] = mat.m3;
    buffer[13] = mat.m7;
    buffer[14] = mat.m11;
    buffer[15] = mat.m15;

    return buffer;
}
#endif
