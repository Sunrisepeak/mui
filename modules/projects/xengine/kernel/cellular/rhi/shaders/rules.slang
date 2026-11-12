// Encoding v1 is prepared by openmui.simulation.cellular, not authored here.
// Unsigned integer predicates and saturating writes have exact CPU semantics.
[[vk::binding(0, 0)]] StructuredBuffer<uint> before : register(t0, space0);
[[vk::binding(1, 0)]] StructuredBuffer<uint> rules : register(t1, space0);
[[vk::binding(0, 1)]] RWStructuredBuffer<uint> after : register(u0, space1);
struct Params { uint width; uint height; uint channels; uint edge; uint outside; uint pad0; uint pad1; uint pad2; };
[[vk::binding(0, 2)]] ConstantBuffer<Params> params : register(b0, space2);

uint sample_field(uint channel,int x,int y) {
    int w=int(params.width),h=int(params.height);
    if(x<0 || x>=w || y<0 || y>=h) {
        if(params.edge==0)return params.outside;
        if(params.edge==1) {x=clamp(x,0,w-1);y=clamp(y,0,h-1);}
        else {x=x%w;y=y%h;if(x<0)x+=w;if(y<0)y+=h;}
    }
    return before[channel*params.width*params.height+uint(y)*params.width+uint(x)];
}
[shader("compute")]
[numthreads(8,8,1)]
void evolve(uint3 id : SV_DispatchThreadID) {
    if(id.x>=params.width || id.y>=params.height)return;
    uint cells=params.width*params.height,cell=id.y*params.width+id.x;
    for(uint channel=0;channel<params.channels;++channel)after[channel*cells+cell]=before[channel*cells+cell];
    for(uint r=0;r<rules[2];++r) {
        uint start=rules[4+r],pos=start+4;bool matches=true;
        for(uint c=0;c<rules[start+1];++c) {
            uint kind=rules[pos],channel=rules[pos+1],low=rules[pos+2],high=rules[pos+3],count=rules[pos+6];
            if(kind==0) {uint value=before[channel*cells+cell];matches=value>=low && value<=high;}
            else {
                uint found=0;
                for(uint o=0;o<count;++o) {
                    uint value=sample_field(channel,int(id.x)+int(rules[pos+7+2*o])-32,int(id.y)+int(rules[pos+8+2*o])-32);
                    if(value>=low && value<=high)++found;
                }
                matches=found>=rules[pos+4] && found<=rules[pos+5];
            }
            if(!matches)break;
            pos+=7+2*count;
        }
        if(!matches)continue;
        pos=rules[start+3];
        for(uint w=0;w<rules[start+2];++w) {
            uint index=rules[pos]*cells+cell,old=before[index],kind=rules[pos+1],value=rules[pos+2],next=value;
            if(kind==1)next=old+min(value,0xffffffffu-old);
            else if(kind==2)next=old-min(old,value);
            after[index]=next;pos+=3;
        }
        break;
    }
}
