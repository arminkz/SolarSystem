#include "MultiPassRenderer.h"

#include "vulkan/VulkanHelper.h"
#include "vulkan/resources/TextureSampler.h"
#include "core/AssetPath.h"


MultiPassRenderer::MultiPassRenderer(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<SwapChain> swapChain)
    : Renderer(std::move(ctx), std::move(swapChain))
{
    _msaaSamples = VulkanHelper::getMaxMsaaSampleCount(_ctx);

    _sceneInfo.view           = glm::mat4(1.0f);
    _sceneInfo.projection     = glm::mat4(1.0f);
    _sceneInfo.time           = 0.0f;
    _sceneInfo.cameraPosition = glm::vec3(0.0f);
    _sceneInfo.lightColor     = glm::vec3(1.0f);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        _sceneInfoUBOs[i] = std::make_unique<Buffer>(_ctx, sizeof(SceneInfo),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        _sceneDescriptorSets[i] = std::make_unique<DescriptorSet>(_ctx, std::vector<Descriptor>{
            Descriptor(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                1, _sceneInfoUBOs[i]->getDescriptorInfo())
        });
    }

    createRenderPasses();
    createFrameBuffers();
    createPostProcessPipelines();
    createObjectSelectionPipeline();
}


MultiPassRenderer::~MultiPassRenderer()
{
    vkDeviceWaitIdle(_ctx->device);

    _mainRenderPass = nullptr;
    _noMSAARenderPass = nullptr;
    _compositeRenderPass = nullptr;
    _objectSelectionRenderPass = nullptr;
}


void MultiPassRenderer::createRenderPasses()
{
    RenderPassParams offscreenParams;
    offscreenParams.name                    = "Offscreen Renderpass - No MSAA";
    offscreenParams.colorFormat             = VK_FORMAT_R8G8B8A8_SRGB;
    offscreenParams.depthFormat             = VulkanHelper::findDepthFormat(_ctx);
    offscreenParams.colorAttachmentLayout   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    offscreenParams.depthAttachmentLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    offscreenParams.useColor                = true;
    offscreenParams.useDepth                = true;
    offscreenParams.useResolve              = false;
    offscreenParams.msaaSamples             = VK_SAMPLE_COUNT_1_BIT;
    _noMSAARenderPass = std::make_unique<RenderPass>(_ctx, offscreenParams);

    offscreenParams.name                    = "Main (Offscreen) Renderpass - MSAA";
    offscreenParams.resolveFormat           = VK_FORMAT_R8G8B8A8_SRGB;
    offscreenParams.colorAttachmentLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    offscreenParams.resolveAttachmentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    offscreenParams.useResolve              = true;
    offscreenParams.msaaSamples             = _msaaSamples;
    _mainRenderPass = std::make_unique<RenderPass>(_ctx, offscreenParams);

    RenderPassParams screenParams;
    screenParams.name                    = "Onscreen Renderpass";
    screenParams.colorFormat             = _swapChain->getSwapChainImageFormat();
    screenParams.depthFormat             = VulkanHelper::findDepthFormat(_ctx);
    screenParams.resolveFormat           = _swapChain->getSwapChainImageFormat();
    screenParams.colorAttachmentLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    screenParams.depthAttachmentLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    screenParams.resolveAttachmentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    screenParams.useColor                = true;
    screenParams.useDepth                = true;
    screenParams.useResolve              = true;
    screenParams.msaaSamples             = _msaaSamples;
    screenParams.isMultiPass             = false;
    _compositeRenderPass = std::make_unique<RenderPass>(_ctx, screenParams);

    RenderPassParams selectionParams;
    selectionParams.name                  = "Object Selection Renderpass";
    selectionParams.colorFormat           = VK_FORMAT_R32_UINT;
    selectionParams.depthFormat           = VulkanHelper::findDepthFormat(_ctx);
    selectionParams.colorAttachmentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    selectionParams.depthAttachmentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    selectionParams.useColor              = true;
    selectionParams.useDepth              = true;
    selectionParams.useResolve            = false;
    selectionParams.msaaSamples           = VK_SAMPLE_COUNT_1_BIT;
    selectionParams.isMultiPass           = false;
    _objectSelectionRenderPass = std::make_unique<RenderPass>(_ctx, selectionParams);
}


void MultiPassRenderer::createFrameBuffers()
{
    _offscreenFrameBuffers.resize(4);

    FrameBufferParams offscreenParams{};
    offscreenParams.extent.width   = static_cast<int>(_swapChain->getSwapChainExtent().width);
    offscreenParams.extent.height  = static_cast<int>(_swapChain->getSwapChainExtent().height);
    offscreenParams.renderPass     = _mainRenderPass->getRenderPass();
    offscreenParams.hasColor       = true;
    offscreenParams.hasDepth       = true;
    offscreenParams.hasResolve     = true;
    offscreenParams.msaaSamples    = _msaaSamples;
    offscreenParams.colorFormat    = VK_FORMAT_R8G8B8A8_SRGB;
    offscreenParams.depthFormat    = VulkanHelper::findDepthFormat(_ctx);
    offscreenParams.resolveFormat  = VK_FORMAT_R8G8B8A8_SRGB;
    offscreenParams.colorUsage     = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    offscreenParams.depthUsage     = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    offscreenParams.resolveUsage   = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    _offscreenFrameBuffers[0] = std::make_unique<FrameBuffer>(_ctx, offscreenParams); // glow
    _offscreenFrameBuffers[1] = std::make_unique<FrameBuffer>(_ctx, offscreenParams); // vertical blur
    _offscreenFrameBuffers[2] = std::make_unique<FrameBuffer>(_ctx, offscreenParams); // horizontal blur
    _offscreenFrameBuffers[3] = std::make_unique<FrameBuffer>(_ctx, offscreenParams); // scene

    FrameBufferParams mainParams{};
    mainParams.extent       = _swapChain->getSwapChainExtent();
    mainParams.renderPass   = _compositeRenderPass->getRenderPass();
    mainParams.msaaSamples  = _msaaSamples;
    mainParams.hasColor     = true;
    mainParams.hasDepth     = true;
    mainParams.hasResolve   = true;
    mainParams.colorFormat  = _swapChain->getSwapChainImageFormat();
    mainParams.depthFormat  = VulkanHelper::findDepthFormat(_ctx);
    mainParams.colorUsage   = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    mainParams.depthUsage   = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    _mainFrameBuffers.resize(_swapChain->getSwapChainImageViews().size());
    for (size_t i = 0; i < _swapChain->getSwapChainImageViews().size(); i++) {
        mainParams.resolveImageView = _swapChain->getSwapChainImageViews()[i];
        _mainFrameBuffers[i] = std::make_unique<FrameBuffer>(_ctx, mainParams);
    }

    FrameBufferParams selectionParams{};
    selectionParams.extent       = _swapChain->getSwapChainExtent();
    selectionParams.renderPass   = _objectSelectionRenderPass->getRenderPass();
    selectionParams.msaaSamples  = VK_SAMPLE_COUNT_1_BIT;
    selectionParams.hasColor     = true;
    selectionParams.hasDepth     = true;
    selectionParams.hasResolve   = false;
    selectionParams.colorFormat  = VK_FORMAT_R32_UINT;
    selectionParams.depthFormat  = VulkanHelper::findDepthFormat(_ctx);
    selectionParams.colorUsage   = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    selectionParams.depthUsage   = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    _objectSelectionFrameBuffer = std::make_unique<FrameBuffer>(_ctx, selectionParams);
}


void MultiPassRenderer::createPostProcessPipelines()
{
    VkDescriptorSetLayout sceneDSL = _sceneDescriptorSets[0]->getDescriptorSetLayout();

    _ppTextureSampler = std::make_unique<TextureSampler>(_ctx, 1, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    // Glow pass pipeline
    PipelineParams glowPassParams;
    glowPassParams.name = "GlowPassPipeline";
    glowPassParams.descriptorSetLayouts = {sceneDSL};
    glowPassParams.pushConstantRanges   = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GlowPassPushConstants)}};
    glowPassParams.renderPass           = _mainRenderPass->getRenderPass();
    glowPassParams.msaaSamples          = _msaaSamples;
    _glowPipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/glow/glow_vert.spv"),
        AssetPath::getInstance()->get("spv/glow/glow_frag.spv"),
        glowPassParams);

    // Blur settings UBO
    _blurSettingsUBO = std::make_unique<Buffer>(_ctx, sizeof(BlurSettings),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    _blurSettingsUBO->copyData(&_blurSettings, sizeof(_blurSettings));

    // Build descriptor sets for blur + composite (reads from offscreen FB image views)
    createPostProcessDescriptors();

    // Blur pass pipeline (vertical)
    PipelineParams blurParams{};
    blurParams.name                        = "BlurPassPipeline - Vertical";
    blurParams.vertexBindingDescription    = std::nullopt;
    blurParams.vertexAttributeDescriptions = {};
    blurParams.cullMode                    = VK_CULL_MODE_NONE;
    blurParams.descriptorSetLayouts        = {_blurVertDescriptorSet->getDescriptorSetLayout()};
    blurParams.pushConstantRanges          = {};
    blurParams.renderPass                  = _mainRenderPass->getRenderPass();
    blurParams.msaaSamples                 = _msaaSamples;
    int blurDirection = 0;
    VkSpecializationMapEntry blurMapEntry  = {0, 0, sizeof(int)};
    blurParams.fragmentShaderSpecializationInfo = VkSpecializationInfo{1, &blurMapEntry, sizeof(int), &blurDirection};
    _blurVertPipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/blur/blur_vert.spv"),
        AssetPath::getInstance()->get("spv/blur/blur_frag.spv"),
        blurParams);

    // Blur pass pipeline (horizontal)
    blurParams.name           = "BlurPassPipeline - Horizontal";
    blurParams.descriptorSetLayouts = {_blurHorizDescriptorSet->getDescriptorSetLayout()};
    blurDirection = 1;
    blurParams.fragmentShaderSpecializationInfo = VkSpecializationInfo{1, &blurMapEntry, sizeof(int), &blurDirection};
    _blurHorizPipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/blur/blur_vert.spv"),
        AssetPath::getInstance()->get("spv/blur/blur_frag.spv"),
        blurParams);

    // Composite pipeline
    PipelineParams compositeParams{};
    compositeParams.name                        = "CompositePipeline";
    compositeParams.vertexBindingDescription    = std::nullopt;
    compositeParams.vertexAttributeDescriptions = {};
    compositeParams.cullMode                    = VK_CULL_MODE_NONE;
    compositeParams.descriptorSetLayouts        = {_compositeDescriptorSet->getDescriptorSetLayout()};
    compositeParams.pushConstantRanges          = {};
    compositeParams.renderPass                  = _compositeRenderPass->getRenderPass();
    compositeParams.msaaSamples                 = _msaaSamples;
    _compositePipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/composite/composite_vert.spv"),
        AssetPath::getInstance()->get("spv/composite/composite_frag.spv"),
        compositeParams);
}


void MultiPassRenderer::createPostProcessDescriptors()
{
    VkDescriptorImageInfo glowOut{};
    glowOut.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    glowOut.imageView   = _offscreenFrameBuffers[0]->getResolveImageView();
    glowOut.sampler     = _ppTextureSampler->getSampler();

    _blurVertDescriptorSet = std::make_unique<DescriptorSet>(_ctx, std::vector<Descriptor>{
        Descriptor(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT, 1, _blurSettingsUBO->getDescriptorInfo()),
        Descriptor(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, glowOut),
    });

    VkDescriptorImageInfo blurVertOut{};
    blurVertOut.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    blurVertOut.imageView   = _offscreenFrameBuffers[1]->getResolveImageView();
    blurVertOut.sampler     = _ppTextureSampler->getSampler();

    _blurHorizDescriptorSet = std::make_unique<DescriptorSet>(_ctx, std::vector<Descriptor>{
        Descriptor(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT, 1, _blurSettingsUBO->getDescriptorInfo()),
        Descriptor(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, blurVertOut),
    });

    VkDescriptorImageInfo blurHorizOut{};
    blurHorizOut.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    blurHorizOut.imageView   = _offscreenFrameBuffers[2]->getResolveImageView();
    blurHorizOut.sampler     = _ppTextureSampler->getSampler();

    VkDescriptorImageInfo normalOut{};
    normalOut.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalOut.imageView   = _offscreenFrameBuffers[3]->getResolveImageView();
    normalOut.sampler     = _ppTextureSampler->getSampler();

    _compositeDescriptorSet = std::make_unique<DescriptorSet>(_ctx, std::vector<Descriptor>{
        Descriptor(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, blurHorizOut),
        Descriptor(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, normalOut),
    });
}


void MultiPassRenderer::createObjectSelectionPipeline() {
    VkDescriptorSetLayout sceneDSL = _sceneDescriptorSets[0]->getDescriptorSetLayout();

    // Object selection pipeline
    PipelineParams selectionParams;
    selectionParams.name                 = "ObjectSelectionPipeline";
    selectionParams.descriptorSetLayouts = {sceneDSL};
    selectionParams.pushConstantRanges   = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectSelectionPushConstants)}};
    selectionParams.renderPass           = _objectSelectionRenderPass->getRenderPass();
    selectionParams.blendEnable          = false;
    _objectSelectionPipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/selection/select_vert.spv"),
        AssetPath::getInstance()->get("spv/selection/select_frag.spv"),
        selectionParams);
}


void MultiPassRenderer::update()
{
    VkExtent2D swapChainExtent = _swapChain->getSwapChainExtent();

    // Renderer specific updates (View, Projection, SceneUBO)
    _sceneInfo.view = _camera->getViewMatrix();
    _sceneInfo.projection = glm::perspective(glm::radians(45.f),(float)swapChainExtent.width / (float)swapChainExtent.height, 0.1f, 4000.f);
    _sceneInfo.projection[1][1] *= -1;
    _sceneInfo.cameraPosition = _camera->getPosition();
    
    // Application specific updates (present in child class)
    advance();

    // Flush Scene UBO
    _sceneInfoUBOs[_currentFrame]->copyData(&_sceneInfo, sizeof(_sceneInfo));
}


void MultiPassRenderer::recordToCommandBuffer(VkCommandBuffer commandBuffer, uint32_t targetSwapImageIndex)
{
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.float32[3] = 1.0f;   // r=g=b=0, a=1
    clearValues[1].depthStencil     = {1.0f, 0};

    auto beginOffscreenPass = [&](int fbIndex) {
        VkRenderPassBeginInfo info{};
        info.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass        = _mainRenderPass->getRenderPass();
        info.framebuffer       = _offscreenFrameBuffers[fbIndex]->getFrameBuffer();
        info.renderArea.offset = {0, 0};
        info.renderArea.extent = _offscreenFrameBuffers[fbIndex]->getExtent();
        info.clearValueCount   = static_cast<uint32_t>(clearValues.size());
        info.pClearValues      = clearValues.data();
        vkCmdBeginRenderPass(commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        vp.width    = static_cast<float>(_offscreenFrameBuffers[fbIndex]->getExtent().width);
        vp.height   = static_cast<float>(_offscreenFrameBuffers[fbIndex]->getExtent().height);
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &vp);

        VkRect2D sc{};
        sc.extent = _offscreenFrameBuffers[fbIndex]->getExtent();
        vkCmdSetScissor(commandBuffer, 0, 1, &sc);
    };

    /*
        Pass 1 — Glow extraction
    */
    beginOffscreenPass(0);
    for (auto& entry : _glowPassModels) {
        if (entry.useGlowPipeline)
            drawWithGlowPipeline(commandBuffer, *entry.model, entry.glowColor);
        else
            entry.model->draw(commandBuffer, *this);
    }
    vkCmdEndRenderPass(commandBuffer);

    /*
        Pass 2 — Vertical blur
    */
    beginOffscreenPass(1);
    _blurVertPipeline->bind(commandBuffer);
    std::array<VkDescriptorSet, 1> blurVertDS = {_blurVertDescriptorSet->getDescriptorSet()};
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        _blurVertPipeline->getPipelineLayout(), 0, 1, blurVertDS.data(), 0, nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    /*
        Pass 3 — Horizontal blur
    */
    beginOffscreenPass(2);
    _blurHorizPipeline->bind(commandBuffer);
    std::array<VkDescriptorSet, 1> blurHorizDS = {_blurHorizDescriptorSet->getDescriptorSet()};
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        _blurHorizPipeline->getPipelineLayout(), 0, 1, blurHorizDS.data(), 0, nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    /*
        Pass 4 — Normal shading
    */
    beginOffscreenPass(3);
    for (auto& model : _sceneModels)
        model->draw(commandBuffer, *this);
    vkCmdEndRenderPass(commandBuffer);

    /*
        Pass 5 — Composite
    */
    VkRenderPassBeginInfo compositeInfo{};
    compositeInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    compositeInfo.renderPass        = _compositeRenderPass->getRenderPass();
    compositeInfo.framebuffer       = _mainFrameBuffers[targetSwapImageIndex]->getFrameBuffer();
    compositeInfo.renderArea.offset = {0, 0};
    compositeInfo.renderArea.extent = _swapChain->getSwapChainExtent();
    compositeInfo.clearValueCount   = static_cast<uint32_t>(clearValues.size());
    compositeInfo.pClearValues      = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &compositeInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width    = static_cast<float>(_swapChain->getSwapChainExtent().width);
    vp.height   = static_cast<float>(_swapChain->getSwapChainExtent().height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &vp);

    VkRect2D sc{};
    sc.extent = _swapChain->getSwapChainExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &sc);

    _compositePipeline->bind(commandBuffer);
    std::array<VkDescriptorSet, 1> compositeDS = {_compositeDescriptorSet->getDescriptorSet()};
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        _compositePipeline->getPipelineLayout(), 0, 1, compositeDS.data(), 0, nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
}


void MultiPassRenderer::onSwapChainRecreated()
{
    vkDeviceWaitIdle(_ctx->device);
    _mainFrameBuffers.clear();
    _objectSelectionFrameBuffer.reset();
    createFrameBuffers();
    createPostProcessDescriptors();
}


void MultiPassRenderer::setBloomSettings(float scale, float strength)
{
    _blurSettings.blurScale    = scale;
    _blurSettings.blurStrength = strength;
    _blurSettingsUBO->copyData(&_blurSettings, sizeof(_blurSettings));
}


void MultiPassRenderer::drawWithGlowPipeline(VkCommandBuffer commandBuffer, const Model& model, glm::vec3 glowColor) const
{
    _glowPipeline->bind(commandBuffer);

    VkBuffer vertexBuffers[] = {model.getDeviceMesh()->getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, model.getDeviceMesh()->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    std::array<VkDescriptorSet, 1> descriptorSets = {
        _sceneDescriptorSets[_currentFrame]->getDescriptorSet()
    };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        _glowPipeline->getPipelineLayout(), 0, 1, descriptorSets.data(), 0, nullptr);

    GlowPassPushConstants pushConstants{};
    pushConstants.model     = model.getModelMatrix();
    pushConstants.glowColor = glowColor;
    vkCmdPushConstants(commandBuffer, _glowPipeline->getPipelineLayout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GlowPassPushConstants), &pushConstants);

    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(model.getDeviceMesh()->getIndicesCount()), 1, 0, 0, 0);
}


void MultiPassRenderer::handleMouseClick(float mouseX, float mouseY)
{
    uint32_t objectID = querySelectionImage(mouseX, mouseY);
    if (_currentTargetObjectID == objectID) return;

    //TODO: this is application specific and should not be here
    if (_selectableModels.find(objectID) != _selectableModels.end()) {
        _currentTargetObjectID = objectID;
        _camera->setTargetAnimated(_selectableModels[objectID]->getPosition());
    }
}


void MultiPassRenderer::handleMouseDrag(float dx, float dy)
{
    _camera->rotateHorizontally(static_cast<float>(dx) * 0.005f);
    _camera->rotateVertically(static_cast<float>(dy) * 0.005f);
}


void MultiPassRenderer::handleMouseWheel(float dy)
{
    float zoomDelta = dy * _camera->getRadius() * 0.03f;
    _camera->changeZoom(zoomDelta);
}


uint32_t MultiPassRenderer::querySelectionImage(float mouseX, float mouseY)
{
    VkCommandBuffer cmdBuffer = VulkanHelper::beginSingleTimeCommands(_ctx);

    VkRenderPassBeginInfo info{};
    info.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass        = _objectSelectionRenderPass->getRenderPass();
    info.framebuffer       = _objectSelectionFrameBuffer->getFrameBuffer();
    info.renderArea.offset = {0, 0};
    info.renderArea.extent = _objectSelectionFrameBuffer->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.uint32[0] = 0;
    clearValues[1].depthStencil    = {1.0f, 0};
    info.clearValueCount = static_cast<uint32_t>(clearValues.size());
    info.pClearValues    = clearValues.data();

    vkCmdBeginRenderPass(cmdBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width    = static_cast<float>(_objectSelectionFrameBuffer->getExtent().width);
    vp.height   = static_cast<float>(_objectSelectionFrameBuffer->getExtent().height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmdBuffer, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset = {static_cast<int32_t>(mouseX), static_cast<int32_t>(mouseY)};
    scissor.extent = {1, 1};
    vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

    _objectSelectionPipeline->bind(cmdBuffer);

    for (const auto& pair : _selectableModels) {
        VkBuffer vertexBuffers[] = {pair.second->getDeviceMesh()->getVertexBuffer()};
        VkDeviceSize offsets[]   = {0};
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmdBuffer, pair.second->getDeviceMesh()->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        std::array<VkDescriptorSet, 1> descriptorSets = {
            _sceneDescriptorSets[_currentFrame]->getDescriptorSet()
        };
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            _objectSelectionPipeline->getPipelineLayout(), 0, 1, descriptorSets.data(), 0, nullptr);

        ObjectSelectionPushConstants pushConstants{pair.second->getModelMatrix(), static_cast<uint32_t>(pair.second->getID())};
        vkCmdPushConstants(cmdBuffer, _objectSelectionPipeline->getPipelineLayout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectSelectionPushConstants), &pushConstants);

        vkCmdDrawIndexed(cmdBuffer, static_cast<uint32_t>(pair.second->getDeviceMesh()->getIndicesCount()), 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmdBuffer);
    VulkanHelper::endSingleTimeCommands(_ctx, cmdBuffer);

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkDeviceSize imageSize = _swapChain->getSwapChainExtent().width *
                             _swapChain->getSwapChainExtent().height * sizeof(uint32_t);
    VulkanHelper::createBuffer(_ctx, imageSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        false, stagingBuffer, stagingMemory);

    VulkanHelper::copyImageToBuffer(_ctx, _objectSelectionFrameBuffer->getColorImage(),
        stagingBuffer, _swapChain->getSwapChainExtent().width, _swapChain->getSwapChainExtent().height);

    uint32_t* pixelData = new uint32_t[_swapChain->getSwapChainExtent().width * _swapChain->getSwapChainExtent().height];
    void* data;
    vkMapMemory(_ctx->device, stagingMemory, 0, imageSize, 0, &data);
    memcpy(pixelData, data, static_cast<size_t>(imageSize));
    vkUnmapMemory(_ctx->device, stagingMemory);
    vkDestroyBuffer(_ctx->device, stagingBuffer, nullptr);
    vkFreeMemory(_ctx->device, stagingMemory, nullptr);

    int mousePixel       = static_cast<int>(mouseX) + static_cast<int>(mouseY) * _swapChain->getSwapChainExtent().width;
    uint32_t selectedID  = pixelData[mousePixel];
    delete[] pixelData;
    return selectedID;
}
