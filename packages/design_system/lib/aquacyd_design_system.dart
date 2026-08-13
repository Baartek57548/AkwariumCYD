library;

import 'package:flutter/material.dart';

abstract final class ProductSpacing {
  static const double xxs = 4;
  static const double xs = 8;
  static const double sm = 12;
  static const double md = 16;
  static const double lg = 24;
  static const double xl = 32;
  static const double xxl = 48;
}

abstract final class ProductRadius {
  static const double control = 16;
  static const double card = 22;
  static const double hero = 28;
  static const double pill = 999;
}

abstract final class ProductColors {
  static const Color aqua = Color(0xFF18C7B5);
  static const Color ocean = Color(0xFF087EA4);
  static const Color indigo = Color(0xFF4254D0);
  static const Color night = Color(0xFF071619);
  static const Color success = Color(0xFF27AE60);
  static const Color warning = Color(0xFFE3A008);
}

final class HomeControlMark extends StatelessWidget {
  const HomeControlMark({required this.size, super.key});

  final double size;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      image: true,
      label: 'Home Control',
      child: CustomPaint(
        size: Size.square(size),
        painter: _HomeControlMarkPainter(),
      ),
    );
  }
}

final class _HomeControlMarkPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    final radius = Radius.circular(size.width * 0.28);
    final background = Paint()
      ..shader = const LinearGradient(
        begin: Alignment.topLeft,
        end: Alignment.bottomRight,
        colors: <Color>[ProductColors.aqua, ProductColors.indigo],
      ).createShader(rect);
    canvas.drawRRect(RRect.fromRectAndRadius(rect, radius), background);

    final line = Paint()
      ..color = Colors.white
      ..style = PaintingStyle.stroke
      ..strokeWidth = size.width * 0.075
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round;
    final house = Path()
      ..moveTo(size.width * 0.2, size.height * 0.5)
      ..lineTo(size.width * 0.5, size.height * 0.23)
      ..lineTo(size.width * 0.8, size.height * 0.5)
      ..lineTo(size.width * 0.8, size.height * 0.78)
      ..lineTo(size.width * 0.2, size.height * 0.78)
      ..close();
    canvas.drawPath(house, line);
    canvas.drawCircle(
      Offset(size.width * 0.5, size.height * 0.55),
      size.width * 0.105,
      Paint()..color = Colors.white,
    );
    canvas.drawLine(
      Offset(size.width * 0.5, size.height * 0.55),
      Offset(size.width * 0.68, size.height * 0.69),
      line..strokeWidth = size.width * 0.055,
    );
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}
