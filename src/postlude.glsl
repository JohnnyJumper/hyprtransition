
void main() {
    vec4 c = effect(v_uv, u_time);
    gl_FragColor = vec4(c.rgb * c.a, c.a);
}
