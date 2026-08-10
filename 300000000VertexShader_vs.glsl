#if 0
	***	[Hash 0x010d84dd]	2
	***	[Hash 0x01cc1123]	0
	***	[Hash 0x0b4678e4]	1
	***	[Hash 0x153ac2af]	2
	***	[Hash 0x1a4c63d5]	-2126167738
	***	[Hash 0x1c7ad01d]	0
	***	[Hash 0x25dc73c6]	635204550
	***	[Hash 0x277ebc85]	0
	***	[Hash 0x2abbd528]	1
	***	[Hash 0x38942648]	2
	***	[Hash 0x3b038020]	0
	***	[Hash 0x40f321ea]	0
	***	[Hash 0x419acab7]	0
	***	[Hash 0x47e65364]	1
	***	[Hash 0x4ecb6a4b]	0
	***	[Hash 0x4f207a00]	0
	***	[Hash 0x5ad27ea7]	0
	***	[Hash 0x5b0b2416]	0
	***	[Hash 0x61e63948]	1
	***	[Hash 0x66e44c23]	1726237731
	***	[Hash 0x675280f4]	1
	***	[Hash 0x6d28c426]	1
	***	[Hash 0x6dc6cf58]	1841745752
	***	[Hash 0x74824502]	0
	***	[Hash 0x74a7bb70]	1
	***	[Hash 0x76d0dbe7]	1
	***	[Hash 0x79c337a5]	0
	***	[Hash 0x7e693a86]	0
	***	[Hash 0x81454146]	-2126167738
	***	[Hash 0x816c6186]	0
	***	[Hash 0x840f5b80]	0
	***	[Hash 0x8421366d]	1024
	***	[Hash 0x875516cf]	0
	***	[Hash 0x8a18e7aa]	-1978079318
	***	[Hash 0x928aef67]	0
	***	[Hash 0x93f2327b]	635204550
	***	[Hash 0x98e2915b]	0
	***	[Hash 0x9abd84b5]	-1698855755
	***	[Hash 0xa1b3cd70]	1
	***	[Hash 0xa7fb3663]	1
	***	[Hash 0xafaf1bb3]	450
	***	[Hash 0xb967bb7b]	0
	***	[Hash 0xc5ed03e2]	0
	***	[Hash 0xd88cb98c]	1
	***	[Hash 0xe0f2a51a]	2
	***	[Hash 0xe1f4c441]	1
	***	[Hash 0xe60012f8]	1
	***	[Hash 0xeb9401b3]	0
	***	[Hash 0xebb7ea44]	1
	***	[Hash 0xebcb8569]	-338983575
	***	[Hash 0xec133132]	-334286542
	***	[Hash 0xf742ff75]	1
	DONE DUMPING PROPERTIES
	***	[Hash 0x01fb3e75]	xy
	DONE DUMPING PIECES
#endif

	
		#version 430 core
	




	#extension GL_ARB_shading_language_420pack: require
	#define layout_constbuffer(x) layout( std140, x )



	#define bufferFetch texelFetch
	#define structuredBufferFetch texelFetch

	#define min3( a, b, c ) min( a, min( b, c ) )
	#define max3( a, b, c ) max( a, max( b, c ) )

#define float2 vec2
#define float3 vec3
#define float4 vec4

#define int2 ivec2
#define int3 ivec3
#define int4 ivec4

#define uint2 uvec2
#define uint3 uvec3
#define uint4 uvec4

#define float2x2 mat2
#define float3x3 mat3
#define float4x4 mat4
#define ogre_float4x3 mat3x4

#define ushort uint
#define ushort3 uint3
#define ushort4 uint4

//Short used for read operations. It's an int in GLSL & HLSL. An ushort in Metal
#define rshort int
#define rshort2 int2
#define rint int
//Short used for write operations. It's an int in GLSL. An ushort in HLSL & Metal
#define wshort2 int2
#define wshort3 int3

#define toFloat3x3( x ) mat3( x )
#define buildFloat3x3( row0, row1, row2 ) mat3( row0, row1, row2 )

#define buildFloat4x4( row0, row1, row2, row3 ) mat4( row0, row1, row2, row3 )

#define getMatrixRow( mat, idx ) mat[idx]

// Let's explain this madness:
//
// We use the keyword "midf" because "half" is already taken on Metal.
//
// When precision_mode == full32 midf is float. Nothing weird
//
// When precision_mode == midf16, midf and midf_c map both to float16_t. It's similar to full32
// but literals need to be prefixed with _h()
//
// Thus, what happens if we resolve some of the macros, we end up with:
//		float16_t a = 1.0f;						// Error
//		float16_t b = _h( 1.0f );				// OK!
//		float16_t c = float16_t( someFloat );	// OK!
//
// But when precision_mode == relaxed; we have the following problem:
//		mediump float a = 1.0f;							// Error
//		mediump float b = _h( 1.0f );					// OK!
//		mediump float c = mediump float( someFloat );	// Invalid syntax!
//
// That's where 'midf_c' comes into play. The "_c" means cast or construct. Hence we do instead:
//		midf c = midf( someFloat );		// Will turn into invalid syntax on relaxed!
//		midf c = midf_c( someFloat );	// OK!
//
// Therefore datatypes are declared with midf. And casts and constructors are with midf_c
// Proper usage is as follows:
//		midf b = _h( 1.0f );
//		midf b = midf_c( someFloat );
//		midf c = midf3_c( 1.0f, 2.0f, 3.0f );
//
// Using this convention ensures that code will compile with all 3 precision modes.
// Breaking this convention means one or more of the modes (except full32) will not compile.

	#define _h(x) (x)

	#define midf float
	#define midf2 vec2
	#define midf3 vec3
	#define midf4 vec4
	#define midf2x2 mat2
	#define midf3x3 mat3
	#define midf4x4 mat4

	#define midf_c float
	#define midf2_c vec2
	#define midf3_c vec3
	#define midf4_c vec4
	#define midf2x2_c mat2
	#define midf3x3_c mat3
	#define midf4x4_c mat4

	#define midf_tex

	#define toMidf3x3( x ) mat3( x )
	#define buildMidf3x3( row0, row1, row2 ) mat3( row0, row1, row2 )

	#define ensureValidRangeF16(x)

	#define saturate(x) clamp( (x), 0.0, 1.0 )

#define mul( x, y ) ((x) * (y))
#define lerp mix
#define rsqrt inversesqrt
#define INLINE
#define NO_INTERPOLATION_PREFIX flat
#define NO_INTERPOLATION_SUFFIX

#define PARAMS_ARG_DECL
#define PARAMS_ARG


	#define inVs_vertexId gl_VertexID
#define inVs_vertex vertex
#define inVs_normal normal
#define inVs_tangent tangent
#define inVs_binormal binormal
#define inVs_blendWeights blendWeights
#define inVs_blendIndices blendIndices
#define inVs_qtangent qtangent
#define inVs_colour colour


	#define inVs_drawId drawId

#define finalDrawId inVs_drawId


	#define inVs_uv0 uv0
#define outVs_Position gl_Position
#define outVs_viewportIndex gl_ViewportIndex
#define outVs_clipDistance0 gl_ClipDistance[0]

#define gl_SampleMaskIn0 gl_SampleMaskIn[0]
#define reversebits bitfieldReverse

#define outPs_colour0 outColour

	#define OGRE_Sample( tex, sampler, uv ) texture( tex, uv )
	#define OGRE_SampleLevel( tex, sampler, uv, lod ) textureLod( tex, uv, lod )
	#define OGRE_SampleArray2D( tex, sampler, uv, arrayIdx ) texture( tex, vec3( uv, arrayIdx ) )
	#define OGRE_SampleArray2DLevel( tex, sampler, uv, arrayIdx, lod ) textureLod( tex, vec3( uv, arrayIdx ), lod )
	#define OGRE_SampleArrayCubeLevel( tex, sampler, uv, arrayIdx, lod ) textureLod( tex, vec4( uv, arrayIdx ), lod )
	#define OGRE_SampleGrad( tex, sampler, uv, ddx, ddy ) textureGrad( tex, uv, ddx, ddy )
	#define OGRE_SampleArray2DGrad( tex, sampler, uv, arrayIdx, ddx, ddy ) textureGrad( tex, vec3( uv, arrayIdx ), ddx, ddy )

	#define texture2D sampler2D
	#define texture2DArray sampler2DArray
	#define texture3D sampler3D
	#define textureCube samplerCube
	#define textureCubeArray samplerCubeArray

	#define OGRE_Load2DF16( tex, iuv, lod ) midf4_c( texelFetch( tex, ivec2( iuv ), lod ) )
	#define OGRE_Load2DMSF16( tex, iuv, subsample ) midf4_c( texelFetch( tex, iuv, subsample ) )
	#define OGRE_SampleF16( tex, sampler, uv ) midf4_c( texture( tex, uv ) )
	#define OGRE_SampleLevelF16( tex, sampler, uv, lod ) midf4_c( textureLod( tex, uv, lod ) )
	#define OGRE_SampleArray2DF16( tex, sampler, uv, arrayIdx ) midf4_c( texture( tex, vec3( uv, arrayIdx ) ) )
	#define OGRE_SampleArray2DLevelF16( tex, sampler, uv, arrayIdx, lod ) midf4_c( textureLod( tex, vec3( uv, arrayIdx ), lod ) )
	#define OGRE_SampleArrayCubeLevelF16( tex, sampler, uv, arrayIdx, lod ) midf4_c( textureLod( tex, vec4( uv, arrayIdx ), lod ) )
	#define OGRE_SampleGradF16( tex, sampler, uv, ddx, ddy ) midf4_c( textureGrad( tex, uv, ddx, ddy ) )
	#define OGRE_SampleArray2DGradF16( tex, sampler, uv, arrayIdx, ddx, ddy ) midf4_c( textureGrad( tex, vec3( uv, arrayIdx ), ddx, ddy ) )
#define OGRE_ddx( val ) dFdx( val )
#define OGRE_ddy( val ) dFdy( val )
#define OGRE_Load2D( tex, iuv, lod ) texelFetch( tex, ivec2( iuv ), lod )
#define OGRE_LoadArray2D( tex, iuv, arrayIdx, lod ) texelFetch( tex, ivec3( iuv, arrayIdx ), lod )
#define OGRE_Load2DMS( tex, iuv, subsample ) texelFetch( tex, iuv, subsample )

#define OGRE_Load3D( tex, iuv, lod ) texelFetch( tex, ivec3( iuv ), lod )


	#define bufferFetch1( buffer, idx ) texelFetch( buffer, idx ).x


	#define OGRE_SAMPLER_ARG_DECL( samplerName )
	#define OGRE_SAMPLER_ARG( samplerName )

	#define CONST_BUFFER( bufferName, bindingPoint ) layout_constbuffer(binding = bindingPoint) uniform bufferName
	#define CONST_BUFFER_STRUCT_BEGIN( structName, bindingPoint ) layout_constbuffer(binding = bindingPoint) uniform structName
	#define CONST_BUFFER_STRUCT_END( variableName ) variableName

			#define ReadOnlyBufferF( slot, varType, varName ) layout(std430, binding = slot) readonly restrict buffer _##varName { varType varName[]; }
		#define ReadOnlyBufferU( slot, varType, varName ) layout(std430, binding = slot) readonly restrict buffer _##varName { varType varName[]; }
		#define ReadOnlyBufferVarF( varType ) varType
		#define readOnlyFetch( bufferVar, idx ) bufferVar[idx]
		#define readOnlyFetch1( bufferVar, idx ) bufferVar[idx]
	

#define OGRE_Texture3D_float4 texture3D

#define OGRE_ArrayTex( declType, varName, arrayCount ) declType varName[arrayCount]

#define FLAT_INTERPOLANT( decl, bindingPoint ) flat decl
#define INTERPOLANT( decl, bindingPoint ) decl

#define OGRE_OUT_REF( declType, variableName ) out declType variableName
#define OGRE_INOUT_REF( declType, variableName ) inout declType variableName

#define OGRE_ARRAY_START( type ) type[](
#define OGRE_ARRAY_END )



out gl_PerVertex
{
	vec4 gl_Position;

};

layout(std140) uniform;

// START UNIFORM GL PRE DECLARATION

// END UNIFORM GL PRE DECLARATION


	
#define UNPACK_MAT4( matrixBuf, pixelIdx ) mat4( matrixBuf[(pixelIdx) << 2u], matrixBuf[((pixelIdx) << 2u)+1u], matrixBuf[((pixelIdx) << 2u)+2u], matrixBuf[((pixelIdx) << 2u)+3u] )

	

	// START UNIFORM DECLARATION
	
	//Uniforms that change per pass
	CONST_BUFFER_STRUCT_BEGIN( PassBuffer, 0 )
	{
		//Vertex shader
		
			float4x4 viewProj[2];
												//Pixel Shader
		float4 invWindowSize;
		
	}
	CONST_BUFFER_STRUCT_END( passBuf );

	
	
		//Uniforms that change per Item/Entity
		CONST_BUFFER( InstanceBuffer, 2 )
		{
			//.x =
			//Contains the material's start index.
			//
			//.y =
			//shadowConstantBias. Send the bias directly to avoid an
			//unnecessary indirection during the shadow mapping pass.
			//Must be loaded with uintBitsToFloat
			//
			//.z =
			//Contains 0 or 1 to index into passBuf.viewProj[]. Only used
			//if hlms_identity_viewproj_dynamic is set.
							uint4 worldMaterialIdx[4096];
					};
	
	
	
	// END UNIFORM DECLARATION

	
		
			// Most common path
			#define masterMatrix worldViewProj
		
	




	vulkan_layout( OGRE_POSITION ) in vec4 vertex;




	vulkan_layout( OGRE_TEXCOORD0 ) in vec2 uv0;



	vulkan_layout( OGRE_DRAWID ) in uint drawId;





	vulkan_layout( location = 0 ) out block
	{
		
    
		
			FLAT_INTERPOLANT( uint drawId, 0 );
						
			INTERPOLANT( float2 uv0, 1 );	
			

	} outVs;


// START UNIFORM GL DECLARATION

	ReadOnlyBufferF( 0, float4, worldMatBuf );



// END UNIFORM GL DECLARATION

void main()
{

	
	
	

	
		
			float4x4 worldViewProj = UNPACK_MAT4( worldMatBuf, finalDrawId );
		

		
			outVs_Position = mul( inVs_vertex, masterMatrix );
		
	

	
		

		

		
			
				outVs.uv0.xy =
						inVs_uv0.xy;
			
		

		
			outVs.drawId = finalDrawId;
		
	

	
	

	

	

	
}
