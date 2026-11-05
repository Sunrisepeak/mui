// An external consumer's own source, compiled with the installed shader API.
struct Vertex { float2 position : TEXCOORD0; };

[shader("vertex")]
float4 consumer_vs(Vertex value) : SV_Position { return float4(value.position, 0, 1); }

[shader("fragment")]
float4 consumer_fs() : SV_Target { return float4(0.2, 0.4, 0.8, 1); }
