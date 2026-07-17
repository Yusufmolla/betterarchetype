#include "gui/NodeCanvasComponent.h"

#include <cmath>

namespace better
{
namespace
{
constexpr auto nodeWidth = 132.0f;
constexpr auto nodeHeight = 64.0f;
constexpr auto pinRadius = 6.0f;

const auto backgroundColour = juce::Colour (0xff101216);
const auto gridColour = juce::Colour (0xff20252c);
const auto nodeColour = juce::Colour (0xff222832);
const auto endpointColour = juce::Colour (0xff26313d);
const auto selectedColour = juce::Colour (0xffd95840);
const auto lineColour = juce::Colour (0xff7e8794);
const auto textColour = juce::Colour (0xffeef1f5);
const auto mutedTextColour = juce::Colour (0xffaeb6c1);
} // namespace

NodeCanvasComponent::NodeCanvasComponent()
{
    setWantsKeyboardFocus (true);
}

void NodeCanvasComponent::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);

    g.setColour (gridColour);

    for (int x = 0; x < getWidth(); x += 32)
        g.drawVerticalLine (x, 0.0f, (float) getHeight());

    for (int y = 0; y < getHeight(); y += 32)
        g.drawHorizontalLine (y, 0.0f, (float) getWidth());

    const auto nodes = getNodes ? getNodes() : std::vector<GraphNodeDescription> {};
    const auto connections = getConnections ? getConnections() : std::vector<GraphConnectionDescription> {};

    auto findNode = [&nodes] (GraphNodeUID uid) -> std::optional<GraphNodeDescription>
    {
        for (const auto& node : nodes)
            if (node.uid == uid)
                return node;

        return std::nullopt;
    };

    g.setColour (lineColour);

    for (const auto& connection : connections)
    {
        const auto source = findNode (connection.source);
        const auto destination = findNode (connection.destination);

        if (! source || ! destination)
            continue;

        auto start = getOutputPin (*source);
        auto end = getInputPin (*destination);
        juce::Path path;
        path.startNewSubPath (start);
        const auto controlOffset = juce::jmax (60.0f, std::abs (end.x - start.x) * 0.42f);
        path.cubicTo (start.translated (controlOffset, 0.0f),
                      end.translated (-controlOffset, 0.0f),
                      end);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

    if (pendingConnectionSource != 0)
    {
        if (const auto source = findNode (pendingConnectionSource))
        {
            juce::Path path;
            const auto start = getOutputPin (*source);
            path.startNewSubPath (start);
            path.lineTo (lastMousePosition);
            g.setColour (selectedColour);
            g.strokePath (path, juce::PathStrokeType (2.0f));
        }
    }

    for (const auto& node : nodes)
        drawNode (g, node);
}

void NodeCanvasComponent::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    lastMousePosition = event.position;

    if (const auto pin = findPinAt (event.position))
    {
        if (pin->isOutput)
        {
            pendingConnectionSource = pin->uid;
            repaint();
            return;
        }

        if (pendingConnectionSource != 0 && pendingConnectionSource != pin->uid)
        {
            if (onConnectionRequested)
                onConnectionRequested (pendingConnectionSource, pin->uid);

            pendingConnectionSource = 0;
            repaint();
            return;
        }
    }

    pendingConnectionSource = 0;

    if (const auto node = findNodeAt (event.position))
    {
        selectedNode = node->uid;
        draggingNode = node->role == GraphNodeRole::module ? node->uid : 0;
        dragOffset = event.position - juce::Point<float> (node->x, node->y);

        if (onNodeSelected)
            onNodeSelected (node->uid);

        repaint();
        return;
    }

    if (const auto connection = findConnectionAt (event.position))
    {
        if (onConnectionRemoveRequested)
            onConnectionRemoveRequested (*connection);

        repaint();
    }
}

void NodeCanvasComponent::mouseDrag (const juce::MouseEvent& event)
{
    lastMousePosition = event.position;

    if (pendingConnectionSource != 0)
    {
        repaint();
        return;
    }

    if (draggingNode == 0)
        return;

    const auto newPosition = event.position - dragOffset;

    if (onNodeMoved)
        onNodeMoved (draggingNode, newPosition);

    repaint();
}

void NodeCanvasComponent::mouseUp (const juce::MouseEvent& event)
{
    juce::ignoreUnused (event);
    draggingNode = 0;
}

bool NodeCanvasComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (selectedNode != inputNodeUID && selectedNode != outputNodeUID && onNodeDeleteRequested)
        {
            onNodeDeleteRequested (selectedNode);
            return true;
        }
    }

    return false;
}

void NodeCanvasComponent::setSelectedNode (GraphNodeUID uid)
{
    selectedNode = uid;
    repaint();
}

juce::Rectangle<float> NodeCanvasComponent::getNodeBounds (const GraphNodeDescription& node) const
{
    return { node.x, node.y, nodeWidth, nodeHeight };
}

juce::Point<float> NodeCanvasComponent::getInputPin (const GraphNodeDescription& node) const
{
    const auto bounds = getNodeBounds (node);
    return { bounds.getX(), bounds.getCentreY() };
}

juce::Point<float> NodeCanvasComponent::getOutputPin (const GraphNodeDescription& node) const
{
    const auto bounds = getNodeBounds (node);
    return { bounds.getRight(), bounds.getCentreY() };
}

std::optional<GraphNodeDescription> NodeCanvasComponent::findNodeAt (juce::Point<float> position) const
{
    const auto nodes = getNodes ? getNodes() : std::vector<GraphNodeDescription> {};

    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
        if (getNodeBounds (*it).contains (position))
            return *it;

    return std::nullopt;
}

std::optional<NodeCanvasComponent::PinHit> NodeCanvasComponent::findPinAt (juce::Point<float> position) const
{
    const auto nodes = getNodes ? getNodes() : std::vector<GraphNodeDescription> {};

    for (const auto& node : nodes)
    {
        if (hasOutputPin (node) && position.getDistanceFrom (getOutputPin (node)) <= pinRadius + 4.0f)
            return PinHit { node.uid, true };

        if (hasInputPin (node) && position.getDistanceFrom (getInputPin (node)) <= pinRadius + 4.0f)
            return PinHit { node.uid, false };
    }

    return std::nullopt;
}

std::optional<GraphConnectionDescription> NodeCanvasComponent::findConnectionAt (juce::Point<float> position) const
{
    const auto nodes = getNodes ? getNodes() : std::vector<GraphNodeDescription> {};
    const auto connections = getConnections ? getConnections() : std::vector<GraphConnectionDescription> {};

    auto findNode = [&nodes] (GraphNodeUID uid) -> std::optional<GraphNodeDescription>
    {
        for (const auto& node : nodes)
            if (node.uid == uid)
                return node;

        return std::nullopt;
    };

    for (const auto& connection : connections)
    {
        const auto source = findNode (connection.source);
        const auto destination = findNode (connection.destination);

        if (! source || ! destination)
            continue;

        if (distanceToLine (position, getOutputPin (*source), getInputPin (*destination)) <= 8.0f)
            return connection;
    }

    return std::nullopt;
}

float NodeCanvasComponent::distanceToLine (juce::Point<float> point, juce::Point<float> a, juce::Point<float> b)
{
    const auto ab = b - a;
    const auto ap = point - a;
    const auto abLengthSquared = (ab.x * ab.x) + (ab.y * ab.y);

    if (abLengthSquared <= 0.0001f)
        return point.getDistanceFrom (a);

    const auto t = juce::jlimit (0.0f, 1.0f, ((ap.x * ab.x) + (ap.y * ab.y)) / abLengthSquared);
    return point.getDistanceFrom (a + (ab * t));
}

bool NodeCanvasComponent::hasInputPin (const GraphNodeDescription& node)
{
    return node.role != GraphNodeRole::input;
}

bool NodeCanvasComponent::hasOutputPin (const GraphNodeDescription& node)
{
    return node.role != GraphNodeRole::output;
}

void NodeCanvasComponent::drawNode (juce::Graphics& g, const GraphNodeDescription& node)
{
    const auto bounds = getNodeBounds (node);
    const auto selected = node.uid == selectedNode;

    g.setColour (node.role == GraphNodeRole::module ? nodeColour : endpointColour);
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (selected ? selectedColour : juce::Colour (0xff3b4451));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, selected ? 2.0f : 1.0f);

    g.setColour (textColour);
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawFittedText (node.name,
                      bounds.toNearestInt().reduced (14, 10),
                      juce::Justification::centred,
                      1);

    if (node.moduleId.isNotEmpty())
    {
        g.setColour (mutedTextColour);
        g.setFont (11.0f);
        g.drawFittedText (node.moduleId,
                          bounds.toNearestInt().withTrimmedTop (40).reduced (12, 0),
                          juce::Justification::centred,
                          1);
    }

    g.setColour (selected ? selectedColour : lineColour);

    if (hasInputPin (node))
        g.fillEllipse (juce::Rectangle<float> (pinRadius * 2.0f, pinRadius * 2.0f).withCentre (getInputPin (node)));

    if (hasOutputPin (node))
        g.fillEllipse (juce::Rectangle<float> (pinRadius * 2.0f, pinRadius * 2.0f).withCentre (getOutputPin (node)));
}
} // namespace better
