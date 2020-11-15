#version 450

layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec2 UV;
layout(location = 1) in mat3 TBN;

void main() {
    FragColor = vec4(1, 1, 1, 1);
}