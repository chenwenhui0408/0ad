/* Copyright (C) 2012 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Implementation of InstancingModelRenderer
 */

#include "precompiled.h"

#include "lib/ogl.h"
#include "maths/Vector3D.h"
#include "maths/Vector4D.h"

#include "ps/CLogger.h"

#include "graphics/Color.h"
#include "graphics/LightEnv.h"
#include "graphics/Model.h"
#include "graphics/ModelDef.h"

#include "renderer/InstancingModelRenderer.h"
#include "renderer/Renderer.h"
#include "renderer/RenderModifiers.h"
#include "renderer/VertexArray.h"


///////////////////////////////////////////////////////////////////////////////////////////////
// InstancingModelRenderer implementation

struct IModelDef : public CModelDefRPrivate
{
	/// Static per-CModel vertex array
	VertexArray m_Array;

	/// Position and normals are static
	VertexArray::Attribute m_Position;
	VertexArray::Attribute m_Normal;
	VertexArray::Attribute m_BlendJoints; // valid iff gpuSkinning == true
	VertexArray::Attribute m_BlendWeights; // valid iff gpuSkinning == true

	/// The number of UVs is determined by the model
	VertexArray::Attribute m_UVs[5];

	/// Indices are the same for all models, so share them
	VertexIndexArray m_IndexArray;

	IModelDef(const CModelDefPtr& mdef, bool gpuSkinning);
};


IModelDef::IModelDef(const CModelDefPtr& mdef, bool gpuSkinning)
	: m_IndexArray(GL_STATIC_DRAW), m_Array(GL_STATIC_DRAW)
{
	size_t numVertices = mdef->GetNumVertices();

	m_Position.type = GL_FLOAT;
	m_Position.elems = 3;
	m_Array.AddAttribute(&m_Position);

	m_Normal.type = GL_FLOAT;
	m_Normal.elems = 3;
	m_Array.AddAttribute(&m_Normal);

	for (size_t i = 0; i < mdef->GetNumUVsPerVertex(); i++)
	{
		m_UVs[i].type = GL_FLOAT;
		m_UVs[i].elems = 2;
		m_Array.AddAttribute(&m_UVs[i]);
	}

	if (gpuSkinning)
	{
		m_BlendJoints.type = GL_UNSIGNED_BYTE;
		m_BlendJoints.elems = 4;
		m_Array.AddAttribute(&m_BlendJoints);

		m_BlendWeights.type = GL_UNSIGNED_BYTE;
		m_BlendWeights.elems = 4;
		m_Array.AddAttribute(&m_BlendWeights);
	}

	m_Array.SetNumVertices(numVertices);
	m_Array.Layout();

	VertexArrayIterator<CVector3D> Position = m_Position.GetIterator<CVector3D>();
	VertexArrayIterator<CVector3D> Normal = m_Normal.GetIterator<CVector3D>();

	ModelRenderer::CopyPositionAndNormals(mdef, Position, Normal);

	for (size_t i = 0; i < mdef->GetNumUVsPerVertex(); i++)
	{
		VertexArrayIterator<float[2]> UVit = m_UVs[i].GetIterator<float[2]>();
		ModelRenderer::BuildUV(mdef, UVit, i);
	}

	if (gpuSkinning)
	{
		VertexArrayIterator<u8[4]> BlendJoints = m_BlendJoints.GetIterator<u8[4]>();
		VertexArrayIterator<u8[4]> BlendWeights = m_BlendWeights.GetIterator<u8[4]>();
		for (size_t i = 0; i < numVertices; ++i)
		{
			const SModelVertex& vtx = mdef->GetVertices()[i];
			for (size_t j = 0; j < 4; ++j)
			{
				BlendJoints[i][j] = vtx.m_Blend.m_Bone[j];
				BlendWeights[i][j] = (u8)(255.f * vtx.m_Blend.m_Weight[j]);
			}
		}
	}

	m_Array.Upload();
	m_Array.FreeBackingStore();

	m_IndexArray.SetNumVertices(mdef->GetNumFaces()*3);
	m_IndexArray.Layout();
	ModelRenderer::BuildIndices(mdef, m_IndexArray.GetIterator());
	m_IndexArray.Upload();
	m_IndexArray.FreeBackingStore();
}


struct InstancingModelRendererInternals
{
	bool gpuSkinning;

	/// Previously prepared modeldef
	IModelDef* imodeldef;

	/// Index base for imodeldef
	u8* imodeldefIndexBase;
};


// Construction and Destruction
InstancingModelRenderer::InstancingModelRenderer(bool gpuSkinning)
{
	m = new InstancingModelRendererInternals;
	m->gpuSkinning = gpuSkinning;
	m->imodeldef = 0;
}

InstancingModelRenderer::~InstancingModelRenderer()
{
	delete m;
}


// Build modeldef data if necessary - we have no per-CModel data
CModelRData* InstancingModelRenderer::CreateModelData(const void* key, CModel* model)
{
	CModelDefPtr mdef = model->GetModelDef();
	IModelDef* imodeldef = (IModelDef*)mdef->GetRenderData(m);

	if (m->gpuSkinning)
 		ENSURE(model->IsSkinned());
	else
		ENSURE(!model->IsSkinned());

	if (!imodeldef)
	{
		imodeldef = new IModelDef(mdef, m->gpuSkinning);
		mdef->SetRenderData(m, imodeldef);
	}

	return new CModelRData(key);
}


void InstancingModelRenderer::UpdateModelData(CModel* UNUSED(model), CModelRData* UNUSED(data), int UNUSED(updateflags))
{
	// We have no per-CModel data
}


// Setup one rendering pass.
void InstancingModelRenderer::BeginPass(int streamflags)
{
	ENSURE(streamflags == (streamflags & (STREAM_POS|STREAM_NORMAL|STREAM_UV0|STREAM_UV1)));
}

// Cleanup rendering pass.
void InstancingModelRenderer::EndPass(int UNUSED(streamflags))
{
	CVertexBuffer::Unbind();
}


// Prepare UV coordinates for this modeldef
void InstancingModelRenderer::PrepareModelDef(const CShaderProgramPtr& shader, int streamflags, const CModelDef& def)
{
	m->imodeldef = (IModelDef*)def.GetRenderData(m);

	ENSURE(m->imodeldef);

	u8* base = m->imodeldef->m_Array.Bind();
	GLsizei stride = (GLsizei)m->imodeldef->m_Array.GetStride();

	m->imodeldefIndexBase = m->imodeldef->m_IndexArray.Bind();

	if (streamflags & STREAM_POS)
		shader->VertexPointer(3, GL_FLOAT, stride, base + m->imodeldef->m_Position.offset);

	if (streamflags & STREAM_NORMAL)
		shader->NormalPointer(GL_FLOAT, stride, base + m->imodeldef->m_Normal.offset);

	if (streamflags & STREAM_UV0)
		shader->TexCoordPointer(GL_TEXTURE0, 2, GL_FLOAT, stride, base + m->imodeldef->m_UVs[0].offset);
	
	if ((streamflags & STREAM_UV1) && def.GetNumUVsPerVertex() >= 2)
		shader->TexCoordPointer(GL_TEXTURE1, 2, GL_FLOAT, stride, base + m->imodeldef->m_UVs[1].offset);

	// GPU skinning requires extra attributes to compute positions/normals
	if (m->gpuSkinning)
	{
		shader->VertexAttribIPointer("a_skinJoints", 4, GL_UNSIGNED_BYTE, stride, base + m->imodeldef->m_BlendJoints.offset);
		shader->VertexAttribPointer("a_skinWeights", 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, base + m->imodeldef->m_BlendWeights.offset);
	}

	shader->AssertPointersBound();
}


// Render one model
void InstancingModelRenderer::RenderModel(const CShaderProgramPtr& shader, int UNUSED(streamflags), CModel* model, CModelRData* UNUSED(data))
{
	CModelDefPtr mdldef = model->GetModelDef();

	if (m->gpuSkinning)
	{
		// Bind matrices for current animation state.
		// Add 1 to NumBones because of the special 'root' bone.
		// HACK: NVIDIA drivers return uniform name with "[0]", Intel Windows drivers without;
		// try uploading both names since one of them should work, and this is easier than
		// canonicalising the uniform names in CShaderProgramGLSL
 		shader->Uniform("skinBlendMatrices[0]", mdldef->GetNumBones() + 1, model->GetAnimatedBoneMatrices());
		shader->Uniform("skinBlendMatrices", mdldef->GetNumBones() + 1, model->GetAnimatedBoneMatrices());
	}

	// render the lot
	size_t numFaces = mdldef->GetNumFaces();

	if (!g_Renderer.m_SkipSubmit)
	{
		// Draw with DrawRangeElements where available, since it might be more efficient
#if CONFIG2_GLES
		glDrawElements(GL_TRIANGLES, (GLsizei)numFaces*3, GL_UNSIGNED_SHORT, m->imodeldefIndexBase);
#else
		pglDrawRangeElementsEXT(GL_TRIANGLES, 0, (GLuint)mdldef->GetNumVertices()-1,
				(GLsizei)numFaces*3, GL_UNSIGNED_SHORT, m->imodeldefIndexBase);
#endif
	}

	// bump stats
	g_Renderer.m_Stats.m_DrawCalls++;
	g_Renderer.m_Stats.m_ModelTris += numFaces;

}
