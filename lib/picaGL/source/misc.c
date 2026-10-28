#include "internal.h"

float _curScale(void);

static GLint _GetScreenWidth()
{
	return pglState->targetWidth;
}

void glClear(GLbitfield mask)
{
	_batchSubmit();
	shaderProgramUse(&pglState->clearShader);

	uint32_t write_mask = 0;

	if(mask & GL_COLOR_BUFFER_BIT)
		write_mask |= GPU_WRITE_COLOR;
	if(mask & GL_DEPTH_BUFFER_BIT)
		write_mask |= GPU_WRITE_DEPTH;
	
	{
		/* only clear the part of the target that is being rendered at this scale */
		float s = _curScale();
		uint32_t cw = (uint32_t)(pglState->targetWidth * s), ch = (uint32_t)(pglState->targetHeight * s);
		_picaViewport(0, pglState->targetWidth - cw, ch, cw);
	}
	
	_picaScissorTest(pglState->scissorState ? 0x3 : 0x0, pglState->scissorY, pglState->scissorX, pglState->scissorY + pglState->scissorHeight, pglState->scissorX + pglState->scissorWidth);

	_picaDepthMap(0, 1.0, 0);
	_picaLogicOp(GPU_LOGICOP_COPY);
	_picaAlphaTest(false, GPU_ALWAYS, 0);
	_picaDepthTestWriteMask(true, GPU_ALWAYS, write_mask);
	_picaCullMode(GPU_CULL_NONE);

	_picaTexUnitEnable(0x00);
	_picaTextureEnvSet(0, &pglState->texenv[PGL_TEXENV_UNTEXTURED]);
	_picaTextureEnvSet(1, &pglState->texenv[PGL_TEXENV_DUMMY]);

	_picaUniformFloat(GPU_VERTEX_SHADER, 0, (float*)&pglState->clearColor, 1);

	_picaAttribBuffersFormat(0, 0XFF, 0x0, 1);
	_picaImmediateBegin(GPU_TRIANGLE_STRIP);

	_picaFixedAttribute(-1.0,  1.0, pglState->clearDepth, 1.0f);
	_picaFixedAttribute(-1.0, -1.0, pglState->clearDepth, 1.0f);
	_picaFixedAttribute( 1.0,  1.0, pglState->clearDepth, 1.0f);
	_picaFixedAttribute( 1.0, -1.0, pglState->clearDepth, 1.0f);

	_picaImmediateEnd();

	shaderProgramUse(&pglState->basicShader);

	pglState->changes = STATE_ALL_CHANGE;
}

void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	//We're passing this as a uniform...
	pglState->clearColor.a = red;
	pglState->clearColor.g = green;
	pglState->clearColor.b = blue;
	pglState->clearColor.r = alpha;
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
	uint8_t mask = (red & 0x1) | ((green & 0x1) << 1) | ((blue & 0x1) << 2) | ((alpha & 0x1) << 3);
	pglState->writeMask = (pglState->writeMask & 0x10) | mask;

	pglState->changes |= STATE_DEPTHTEST_CHANGE;
}

void glCullFace(GLenum mode)
{
	switch(mode)
	{
		case GL_FRONT:
			pglState->cullMode = GPU_CULL_FRONT_CCW;
			break;
		default:
			pglState->cullMode = GPU_CULL_BACK_CCW;
			break;
	}

	pglState->changes |= STATE_CULL_CHANGE;
}

void glFlush(void)
{
	static int nextCommandBuffer = 1;

	_batchSubmit();

	/* wait for the previous command list (it used the other geometry/command buffers) */
	u64 t0 = svcGetSystemTick();
	_queueWaitAndClear();
	pglStats.flushWaitTicks += svcGetSystemTick() - t0;
	pglStats.flushes++;

	_picaFinalize(true);
	u32* commandBuffer;
	u32  commandBuffer_size;

	GPUCMD_Split(&commandBuffer, &commandBuffer_size);

	/* fs2-3ds: clean only what the GPU will read from this list - the command buffer and
	   the used parts of the vertex / index buffers - instead of the whole 32 MB linear heap.
	   (Texture uploads flush their own data.) */
	GX_FlushCacheRegions(commandBuffer, commandBuffer_size * 4,
		(u32*)pglState->geometryBuffer[pglState->geometryBufferCurrent], (pglState->geometryBufferOffset + 31) & ~31u,
		(u32*)pglState->indexBuffer[pglState->geometryBufferCurrent], ((pglState->indexBufferOffset * 2) + 31) & ~31u);
	GX_ProcessCommandList(commandBuffer, commandBuffer_size * 4, 0x00);

	_queueRun(true);

	GPUCMD_SetBuffer(pglState->commandBuffer[nextCommandBuffer], COMMAND_BUFFER_LENGTH, 0);

	pglState->batchedDraws = 0;
	pglState->geometryBufferOffset = 0;
	pglState->indexBufferOffset = 0;
	pglState->geometryBufferCurrent = nextCommandBuffer;

	nextCommandBuffer = !nextCommandBuffer;
}

void glFinish(void)
{

	glFlush();
	_queueWaitAndClear();
}

/*
 * fs2-3ds render scale: while the main target is bound, every viewport and scissor
 * rectangle is multiplied by pglRenderScale. At 0.5 the engine still "sees" 800x480 but
 * the GPU only fills the 400x240 corner of the target (GL bottom-left), i.e. exactly the
 * top LCD's resolution - a quarter of the pixels. Compose targets are never scaled.
 */
float pglRenderScale = 1.0f;
GLint pglViewportRaw[4] = { 0, 0, 800, 480 };
GLint pglScissorRaw[4] = { 0, 0, 800, 480 };

float _curScale(void)
{
	return (pglState->targetColor == pglState->mainColor) ? pglRenderScale : 1.0f;
}

void _applyViewport(void)
{
	float s = _curScale();
	GLint x = (GLint)(pglViewportRaw[0] * s), y = (GLint)(pglViewportRaw[1] * s);
	GLint w = (GLint)(pglViewportRaw[2] * s), h = (GLint)(pglViewportRaw[3] * s);
	GLint vx = (_GetScreenWidth() - w) - x;
	if (pglState->viewportX == vx && pglState->viewportY == y && pglState->viewportWidth == w && pglState->viewportHeight == h)
		return;
	pglState->viewportX = vx;
	pglState->viewportY = y;
	pglState->viewportWidth = w;
	pglState->viewportHeight = h;
	pglState->changes |= STATE_VIEWPORT_CHANGE;
}

void _applyScissor(void)
{
	float s = _curScale();
	GLint x = (GLint)(pglScissorRaw[0] * s), y = (GLint)(pglScissorRaw[1] * s);
	GLint w = (GLint)(pglScissorRaw[2] * s + 0.5f), h = (GLint)(pglScissorRaw[3] * s + 0.5f);
	GLint sx = (_GetScreenWidth() - w) - x;
	if (pglState->scissorX == sx && pglState->scissorY == y && pglState->scissorWidth == w && pglState->scissorHeight == h)
		return;
	pglState->scissorX = sx;
	pglState->scissorY = y;
	pglState->scissorWidth = w;
	pglState->scissorHeight = h;
	pglState->changes |= STATE_SCISSOR_CHANGE;
}

void pglSetRenderScale(float scale)
{
	pglRenderScale = scale;
	_applyViewport();
	_applyScissor();
}

float pglGetRenderScale(void) { return pglRenderScale; }

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	pglViewportRaw[0] = x; pglViewportRaw[1] = y; pglViewportRaw[2] = width; pglViewportRaw[3] = height;
	_applyViewport();
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	pglScissorRaw[0] = x; pglScissorRaw[1] = y; pglScissorRaw[2] = width; pglScissorRaw[3] = height;
	_applyScissor();
}
