function score=get_path_score(soln, obj)

score = sum(sum(s.payoffs0(s.path==0))) ...
        + sum(sum(s.payoffs1(s.path==1))) ...
        - s.num_walls*obj.wall_penalty ...
        - s.num_occlusions*obj.occlusion_penalty;
