#include "pch.h"
#include "PostProcessPass.h"

#include <ui/RuntimePane.h>
#include <utils/loaders/ShaderLoader.h>

namespace arcane {

	PostProcessPass::PostProcessPass(Scene3D *scene) : RenderPass(scene), m_SsaoRenderTarget(Window::getRenderResolutionWidth(), Window::getRenderResolutionHeight(), false), m_SsaoBlurRenderTarget(Window::getRenderResolutionWidth(), Window::getRenderResolutionHeight(), false),
		m_TonemappedNonLinearTarget(Window::getWidth(), Window::getHeight(), false), m_ScreenRenderTarget(Window::getWidth(), Window::getHeight(), false), m_ResolveRenderTarget(Window::getRenderResolutionWidth(), Window::getRenderResolutionHeight(), false), m_BrightPassRenderTarget(Window::getWidth(), Window::getHeight(), false),
		m_BloomFullRenderTarget(Window::getWidth(), Window::getHeight(), false), m_BloomHalfRenderTarget(Window::getWidth() * 0.5f, Window::getHeight() * 0.5f, false), m_BloomQuarterRenderTarget(Window::getWidth() * 0.25f, Window::getHeight() * 0.25f, false), m_BloomEightRenderTarget(Window::getWidth() * 0.125f, Window::getHeight() * 0.125f, false),
		m_FullRenderTarget(Window::getWidth(), Window::getHeight(), false), m_HalfRenderTarget(Window::getWidth() * 0.5f, Window::getHeight() * 0.5f, false), m_QuarterRenderTarget(Window::getWidth() * 0.25f, Window::getWidth() * 0.25f, false), m_EightRenderTarget(Window::getWidth() * 0.125f, Window::getHeight() * 0.125f, false), m_SsaoNoiseTexture(), m_Timer()
	{
		// Shader setup
		m_PostProcessShader = ShaderLoader::loadShader("src/shaders/PostProcess.glsl");
		m_FxaaShader = ShaderLoader::loadShader("src/shaders/FXAA.glsl");
		m_SsaoShader = ShaderLoader::loadShader("src/shaders/SSAO.glsl");
		m_SsaoBlurShader = ShaderLoader::loadShader("src/shaders/SSAO_Blur.glsl");
		m_BloomBrightPassShader = ShaderLoader::loadShader("src/shaders/BloomBrightPass.glsl");
		m_BloomGaussianBlurShader = ShaderLoader::loadShader("src/shaders/BloomGaussianBlur.glsl");

		// Framebuffer setup
		m_SsaoRenderTarget.addColorTexture(NormalizedSingleChannel8).createFramebuffer();
		m_SsaoBlurRenderTarget.addColorTexture(NormalizedSingleChannel8).createFramebuffer();
		m_TonemappedNonLinearTarget.addColorTexture(Normalized8).addDepthStencilRBO(NormalizedDepthOnly).createFramebuffer();
		m_ScreenRenderTarget.addColorTexture(FloatingPoint16).addDepthStencilRBO(NormalizedDepthOnly).createFramebuffer();
		m_ResolveRenderTarget.addColorTexture(FloatingPoint16).addDepthStencilRBO(NormalizedDepthOnly).createFramebuffer();

		m_FullRenderTarget.addColorTexture(FloatingPoint16).createFramebuffer();
		m_HalfRenderTarget.addColorTexture(FloatingPoint16).createFramebuffer();
		m_QuarterRenderTarget.addColorTexture(FloatingPoint16).createFramebuffer();
		m_EightRenderTarget.addColorTexture(FloatingPoint16).createFramebuffer();

		m_BrightPassRenderTarget.addColorTexture(FloatingPoint16).createFramebuffer();
		m_BloomFullRenderTarget.addColorTexture(FloatingPoint16).createFramebuffer();
		m_BloomHalfRenderTarget.addColorTexture(FloatingPoint16).createFramebuffer();
		m_BloomQuarterRenderTarget.addColorTexture(FloatingPoint16).createFramebuffer();
		m_BloomEightRenderTarget.addColorTexture(FloatingPoint16).createFramebuffer();

		// SSAO Hemisphere Sample Generation (tangent space)
		std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
		std::default_random_engine generator;
		for (unsigned int i = 0; i < m_SsaoKernel.size(); i++) {
			// Make sure that the samples aren't perfectly perpendicular to the normal, or depth reconstruction will yield artifacts (so make sure the z value isn't close to 0)
			glm::vec3 hemisphereSample = glm::vec3((randomFloats(generator) * 2.0f) - 1.0f, (randomFloats(generator) * 2.0f) - 1.0f, glm::clamp((double)randomFloats(generator), 0.2, 1.0)); // Z = [0.2, 1] because we want hemisphere in tangent space
			hemisphereSample = glm::normalize(hemisphereSample);

			// Generate more samples closer to the origin of the hemisphere. Since these make for better light occlusion tests
			float scale = (float)i / m_SsaoKernel.size();
			scale = lerp(0.1f, 1.0f, scale * scale);
			hemisphereSample *= scale;

			m_SsaoKernel[i] = hemisphereSample;
		}

		// SSAO Random Rotation Texture (used to apply a random rotation when constructing the change of basis matrix)
		// Random vectors should be in tangent space
		std::array<glm::vec3, 16> noiseSSAO;
		for (unsigned int i = 0; i < noiseSSAO.size(); i++) {
			noiseSSAO[i] = glm::vec3((randomFloats(generator) * 2.0f) - 1.0f, (randomFloats(generator) * 2.0f) - 1.0f, 0.0f);
		}
		TextureSettings ssaoNoiseTextureSettings;
		ssaoNoiseTextureSettings.TextureFormat = GL_RGB16F;
		ssaoNoiseTextureSettings.TextureWrapSMode = GL_REPEAT;
		ssaoNoiseTextureSettings.TextureWrapTMode = GL_REPEAT;
		ssaoNoiseTextureSettings.TextureMinificationFilterMode = GL_NEAREST;
		ssaoNoiseTextureSettings.TextureMagnificationFilterMode = GL_NEAREST;
		ssaoNoiseTextureSettings.TextureAnisotropyLevel = 1.0f;
		ssaoNoiseTextureSettings.HasMips = false;
		m_SsaoNoiseTexture.setTextureSettings(ssaoNoiseTextureSettings);
		m_SsaoNoiseTexture.generate2DTexture(4, 4, GL_RGB, GL_FLOAT, &noiseSSAO[0]);

		// Debug stuff
		DebugPane::bindFxaaEnabled(&m_FxaaEnabled);
		DebugPane::bindGammaCorrectionValue(&m_GammaCorrection);
		DebugPane::bindExposureValue(&m_Exposure);
		DebugPane::bindBloomThresholdValue(&m_BloomThreshold);
		DebugPane::bindSsaoEnabled(&m_SsaoEnabled);
		DebugPane::bindSsaoSampleRadiusValue(&m_SsaoSampleRadius);
		DebugPane::bindSsaoStrengthValue(&m_SsaoStrength);
	}

	PostProcessPass::~PostProcessPass() {}

	// Generates the AO of the scene using SSAO and stores it in a single channel texture
	PreLightingPassOutput PostProcessPass::executePreLightingPass(GeometryPassOutput &geometryData, ICamera *camera) {
#if DEBUG_ENABLED
		glFinish();
		m_Timer.reset();
#endif
		PreLightingPassOutput passOutput;
		if (!m_SsaoEnabled) {
			passOutput.ssaoTexture = TextureLoader::getWhiteTexture();
			return passOutput;
		}

		// Generate the AO factors for the scene
		glViewport(0, 0, m_SsaoRenderTarget.getWidth(), m_SsaoRenderTarget.getHeight());
		m_SsaoRenderTarget.bind();
		m_GLCache->setDepthTest(false);
		m_GLCache->setFaceCull(true);
		m_GLCache->setCullFace(GL_BACK);

		// Setup
		ModelRenderer *modelRenderer = m_ActiveScene->getModelRenderer();

		// Bind the required data to perform SSAO
		m_GLCache->switchShader(m_SsaoShader);

		// Used to tile the noise texture across the screen every 4 texels (because our noise texture is 4x4)
		m_SsaoShader->setUniform("noiseScale", glm::vec2(m_SsaoRenderTarget.getWidth() / 4.0f, m_SsaoRenderTarget.getHeight() / 4.0f));

		m_SsaoShader->setUniform("ssaoStrength", m_SsaoStrength);
		m_SsaoShader->setUniform("sampleRadius", m_SsaoSampleRadius);
		m_SsaoShader->setUniform("sampleRadius2", m_SsaoSampleRadius * m_SsaoSampleRadius);
		m_SsaoShader->setUniform("numKernelSamples", (int)m_SsaoKernel.size());
		m_SsaoShader->setUniformArray("samples", m_SsaoKernel.size(), &m_SsaoKernel[0]);

		m_SsaoShader->setUniform("view", camera->getViewMatrix());
		m_SsaoShader->setUniform("projection", camera->getProjectionMatrix());
		m_SsaoShader->setUniform("viewInverse", glm::inverse(camera->getViewMatrix()));
		m_SsaoShader->setUniform("projectionInverse", glm::inverse(camera->getProjectionMatrix()));

		geometryData.outputGBuffer->getNormal()->bind(0);
		m_SsaoShader->setUniform("normalTexture", 0);
		geometryData.outputGBuffer->getDepthStencilTexture()->bind(1);
		m_SsaoShader->setUniform("depthTexture", 1);
		m_SsaoNoiseTexture.bind(2);
		m_SsaoShader->setUniform("texNoise", 2);

		// Render our NDC quad to perform SSAO
		modelRenderer->NDC_Plane.Draw();


		// Blur the result
		m_SsaoBlurRenderTarget.bind();
		m_SsaoBlurShader->enable();

		m_SsaoBlurShader->setUniform("numSamplesAroundTexel", 2); // 5x5 kernel blur
		m_SsaoBlurShader->setUniform("ssaoInput", 0); // Texture unit
		m_SsaoRenderTarget.getColourTexture()->bind(0);

		// Render our NDC quad to blur our SSAO texture
		modelRenderer->NDC_Plane.Draw();
		
		// Reset unusual state
		m_GLCache->setDepthTest(true);

#if DEBUG_ENABLED
		glFinish();
		RuntimePane::setSsaoTimer((float)m_Timer.elapsed());
#endif

		// Render pass output
		passOutput.ssaoTexture = m_SsaoBlurRenderTarget.getColourTexture();
		return passOutput;
	}

	void PostProcessPass::executePostProcessPass(Framebuffer *framebufferToProcess) {
		ModelRenderer *modelRenderer = m_ActiveScene->getModelRenderer();
		GLCache *glCache = GLCache::getInstance();

		// If the framebuffer is multi-sampled, resolve it
		Framebuffer *supersampledTarget = framebufferToProcess;
		if (framebufferToProcess->isMultisampled()) {
			glBindFramebuffer(GL_READ_FRAMEBUFFER, framebufferToProcess->getFramebuffer());
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ResolveRenderTarget.getFramebuffer());
			glBlitFramebuffer(0, 0, framebufferToProcess->getWidth(), framebufferToProcess->getHeight(), 0, 0, m_ResolveRenderTarget.getWidth(), m_ResolveRenderTarget.getHeight(), GL_COLOR_BUFFER_BIT, GL_NEAREST);
			supersampledTarget = &m_ResolveRenderTarget;
		}

		// If some sort of super-sampling is set, we need to downsample (or upsample) our image to match the window's resolution
		Framebuffer *target = supersampledTarget;
		if (target->getWidth() != m_ScreenRenderTarget.getWidth() || target->getHeight() != m_ScreenRenderTarget.getHeight()) {
			glBindFramebuffer(GL_READ_FRAMEBUFFER, supersampledTarget->getFramebuffer());
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ScreenRenderTarget.getFramebuffer());
			glBlitFramebuffer(0, 0, supersampledTarget->getWidth(), supersampledTarget->getHeight(), 0, 0, m_ScreenRenderTarget.getWidth(), m_ScreenRenderTarget.getHeight(), GL_COLOR_BUFFER_BIT, GL_LINEAR);
			target = &m_ScreenRenderTarget;
		}

#if DEBUG_ENABLED
		if (DebugPane::getWireframeMode())
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

		// Bloom Bright Pass
		glViewport(0, 0, m_BrightPassRenderTarget.getWidth(), m_BrightPassRenderTarget.getHeight());
		m_BrightPassRenderTarget.bind();
		m_BrightPassRenderTarget.clear();
		glCache->switchShader(m_BloomBrightPassShader);
		m_BloomBrightPassShader->setUniform("threshold", m_BloomThreshold);
		m_BloomBrightPassShader->setUniform("scene_capture", 0);
		target->getColourTexture()->bind(0);
		modelRenderer->NDC_Plane.Draw();

		// Bloom Gaussian Blur Pass
		// As the render target gets smaller, we can increase the separable (two-pass) Gaussian kernel size
		glCache->switchShader(m_BloomGaussianBlurShader);
		glViewport(0, 0, m_FullRenderTarget.getWidth(), m_FullRenderTarget.getHeight());
		m_FullRenderTarget.bind();
		m_FullRenderTarget.clear();
		m_BloomGaussianBlurShader->setUniform("isVerticalBlur", true);
		m_BloomGaussianBlurShader->setUniform("read_offset", glm::vec2(1.0f / (float)m_FullRenderTarget.getWidth(), 1.0f / (float)m_FullRenderTarget.getHeight()));
		m_BloomGaussianBlurShader->setUniform("bloom_texture", 0);
		m_BrightPassRenderTarget.getColourTexture()->bind(0);
		modelRenderer->NDC_Plane.Draw();

		m_BloomFullRenderTarget.bind();
		m_BloomFullRenderTarget.clear();
		m_BloomGaussianBlurShader->setUniform("isVerticalBlur", false);
		m_BloomGaussianBlurShader->setUniform("read_offset", glm::vec2(1.0f / (float)m_BloomFullRenderTarget.getWidth(), 1.0f / (float)m_BloomFullRenderTarget.getHeight()));
		m_BloomGaussianBlurShader->setUniform("bloom_texture", 0);
		m_FullRenderTarget.getColourTexture()->bind(0);
		modelRenderer->NDC_Plane.Draw();

		// Set post process settings and convert our scene from HDR (linear) -> SDR (sRGB)
		glViewport(0, 0, m_TonemappedNonLinearTarget.getWidth(), m_TonemappedNonLinearTarget.getHeight());
		m_TonemappedNonLinearTarget.bind();
		m_TonemappedNonLinearTarget.clear();
		glCache->switchShader(m_PostProcessShader);
		m_PostProcessShader->setUniform("gamma_inverse", 1.0f / m_GammaCorrection);
		m_PostProcessShader->setUniform("exposure", m_Exposure);
		m_PostProcessShader->setUniform("scene_capture", 0);
		target->getColourTexture()->bind(0);
		m_PostProcessShader->setUniform("bloom_texture", 1);
		m_BloomFullRenderTarget.getColourTexture()->bind(1);
		modelRenderer->NDC_Plane.Draw();

#if DEBUG_ENABLED
		glFinish();
		m_Timer.reset();
#endif
		// Finally render the scene to the window's framebuffer
		Window::bind();
		Window::clear();
		glCache->switchShader(m_FxaaShader);
		m_FxaaShader->setUniform("enable_FXAA", m_FxaaEnabled);
		m_FxaaShader->setUniform("inverse_resolution", glm::vec2(1.0f / (float)target->getWidth(), 1.0f / (float)target->getHeight()));
		m_FxaaShader->setUniform("colour_texture", 0);
		m_TonemappedNonLinearTarget.getColourTexture()->bind(0);
		modelRenderer->NDC_Plane.Draw();
#if DEBUG_ENABLED
		glFinish();
		RuntimePane::setFxaaTimer((float)m_Timer.elapsed());
#endif
	}

}
